/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_bus.c
 * @brief bm_bus 统一数据总线实现（LATEST/QUEUE/SIGNAL 环后端 + BLOCK 控制反转委托）
 *
 * LATEST / QUEUE / SIGNAL 三种 mode 共用同一套借还逻辑，并发层按
 * BM_CONFIG_CPU_COUNT 切换。零动态分配，写者 O(1) 无阻塞。
 * BLOCK 模式以控制反转方式透传至 bm_block_backend_iface_t 后端，core 层不引用任何 hybrid 类型。
 * @author zeh (china_qzh@163.com)
 * @version 0.9
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       0.1            zeh            Phase 1 Task 2 写路径骨架
 * 2026-06-25       0.2            zeh            Phase 1 Task 3 QUEUE 读路径：reader_attach 修正、acquire_read、release、freeze
 * 2026-06-25       0.3            zeh            Phase 1 Task 4 ready_count 实现；stats Phase 1 注释；SIGNAL 多读者独立游标测试
 * 2026-06-25       0.4            zeh            Phase 1 Task 5 LATEST 三缓冲选槽实现：acquire_write/commit 真实分支；reader_attach 单读者约束；acquire_read spin-until-stable 循环
 * 2026-06-25       0.5            zeh            Phase 1 Task 6 bus_storage_valid LATEST cap>=3 校验 Doxygen 完善；validate/freeze 边界测试覆盖
 * 2026-06-25       0.6            zeh            DET-01 LATEST acquire_read spin-until-stable 重试封顶（BM_CONFIG_BUS_LATEST_MAX_RETRIES），超界非阻塞返回，WCET 可静态分析；新增 BM_ENABLE_BUS_TEST_HOOK 测试缝
 * 2026-06-26       0.7            zeh            Phase 2 BLOCK 控制反转：bm_bus_bind_block_backend + 专用 produce/consume 六入口；open 初始化 block 字段
 * 2026-06-26       0.8            zeh            新增 bm_bus_reset()：freeze 对称解冻/复位，与 bm_event_reset() 语义对称
 * 2026-06-26       0.9            zeh            seqlock 多读者 LATEST 读：latest_seq 字段 + bm_bus_latest_read 增量并存方案
 *
 */
#include "bm/core/bm_bus.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_critical_wrap.h"
#include "bm_log.h"
#include "bm_safety.h"

#include <string.h>

#if defined(BM_ENABLE_BUS_TEST_HOOK)
/* 测试钩子定义（仅测试构建）：见 bm_bus.h 声明与各调用点。 */
void (*bm_bus_test_latest_read_hook)(bm_bus_storage_t *st)       = NULL;
void (*bm_bus_test_latest_multi_read_hook)(bm_bus_storage_t *st) = NULL;
#endif

/* ------------------------------------------------------------------ */
/* 并发层抽象（与 bm_channel.c 风格一致）                               */
/* ------------------------------------------------------------------ */
#if BM_CONFIG_CPU_COUNT > 1u
static inline uint32_t bus_load_cur(const bm_atomic_ipc_u32_t *p) {
    return bm_atomic_ipc_load_u32(p);
}
static inline void bus_store_cur(bm_atomic_ipc_u32_t *p, uint32_t v) {
    bm_atomic_ipc_store_u32(p, v);
}
/* 多核 SPMC：写端由 owner_cpu 独占、读端只读各自游标，无需互斥；
 * 重入保护由 volatile write_in_progress 承担，LOCK/UNLOCK 均为空操作。 */
#define BUS_LOCK(s)   do { (void)(s); } while (0)
#define BUS_UNLOCK(s) do { (void)(s); } while (0)
#else
static inline uint32_t bus_load_cur(const bm_atomic_ipc_u32_t *p) {
    return bm_atomic_ipc_load_u32(p);
}
static inline void bus_store_cur(bm_atomic_ipc_u32_t *p, uint32_t v) {
    bm_atomic_ipc_store_u32(p, v);
}
#define BUS_LOCK(s)   do { *(s) = BM_CRITICAL_ENTER(); } while (0)
#define BUS_UNLOCK(s) BM_CRITICAL_EXIT(s)
#endif

/**
 * @brief 取绝对游标对应的槽索引（取模 capacity）
 *
 * @param cur 绝对单调游标
 * @param cap 环容量
 * @return 槽索引（0..cap-1）
 */
static inline uint32_t bus_slot(uint32_t cur, uint32_t cap) {
    return cur % cap;
}

/**
 * @brief 取槽字节偏移指针
 *
 * @param st  storage 指针
 * @param cur 绝对单调游标
 * @return 指向槽首字节的指针
 */
static inline uint8_t *bus_slot_ptr(bm_bus_storage_t *st, uint32_t cur) {
    return st->data_buf + bus_slot(cur, st->capacity) * st->elem_size;
}

/**
 * @brief 校验 storage 结构性约束（不依赖 open 状态）
 *
 * 通用约束：cap>=2、elem_size>0、mode 合法、QUEUE 隐含 max_consumers==1。
 * LATEST 专属：三缓冲在多核下需避开 published 与 reading 两槽，
 *   必有第三槽可用，故强制 cap>=3（cap=2 退化双缓冲会读写撕裂，破坏确定性）。
 *
 * @param st storage 指针
 * @return BM_OK 通过；BM_ERR_INVALID 不通过
 */
static int bus_storage_valid(const bm_bus_storage_t *st) {
    if (!st || !st->data_buf || !st->readers) {
        return BM_ERR_INVALID;
    }
    if (st->elem_size == 0u || st->capacity < 2u) {
        return BM_ERR_INVALID;
    }
    if (st->mode != BM_BUS_LATEST && st->mode != BM_BUS_QUEUE &&
        st->mode != BM_BUS_SIGNAL && st->mode != BM_BUS_BLOCK) {
        return BM_ERR_INVALID;
    }
    if (st->mode == BM_BUS_QUEUE && st->max_consumers != 1u) {
        return BM_ERR_INVALID;
    }
    /* LATEST 三缓冲多核防撕裂：需第三槽，强制 cap>=3。
     * 写者 choose_slot 需避开 published 与 reading 两槽，cap=2 时无第三槽可用，
     * 双缓冲退化会在多核读写并发时发生数据撕裂，破坏 LATEST 的确定性语义（spec §7）。 */
    if (st->mode == BM_BUS_LATEST && st->capacity < 3u) {
        return BM_ERR_INVALID;
    }
    /* QUEUE/SIGNAL 自由递增游标取模 cap：cap 非 2 的幂时，2^32 回绕处 cur%cap 不连续
     * （2^32 mod cap != 0），相邻游标会撞同槽，回绕瞬间发生一次静默错读/lap 误判。
     * 强制 2 的幂使取模退化为掩码、回绕无缝（Fix 2）。LATEST 用三缓冲 choose 不碰
     * write_cur、BLOCK 委托块流后端，二者豁免。BM_BUS_DEFINE 已加同义编译期断言。 */
    if ((st->mode == BM_BUS_QUEUE || st->mode == BM_BUS_SIGNAL) &&
        (st->capacity & (st->capacity - 1u)) != 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/* ------------------------------------------------------------------ */
/* open / close / validate                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化 bus 句柄，绑定 storage 与 owner_cpu
 *
 * @param h        句柄指针
 * @param storage  BM_BUS_DEFINE 产生的静态存储对象指针
 * @param cfg      配置（owner_cpu）
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_bus_open(bm_bus_t *h, bm_bus_storage_t *storage,
                const bm_bus_cfg_t *cfg) {
    uint32_t i;

    if (!h || !storage || !cfg) {
        return BM_ERR_INVALID;
    }
    if (bus_storage_valid(storage) != BM_OK) {
        return BM_ERR_INVALID;
    }
    /* 重置运行期状态 */
    bus_store_cur(&storage->write_cur, 0u);
    /* LATEST：初始无发布数据，latest_published 设为 BM_BUS_LATEST_NONE，
     * acquire_read 检测 NONE 后返回 BM_ERR_WOULD_BLOCK（尚无有效数据）；
     * 首次 commit 后才设为有效槽索引。QUEUE/SIGNAL 此字段不使用，无副作用。 */
    bus_store_cur(&storage->latest_published, BM_BUS_LATEST_NONE);
    bus_store_cur(&storage->latest_reading, BM_BUS_LATEST_NONE);
    bus_store_cur(&storage->latest_writing, 0u);  /* scratch，commit 前不读取 */
    bus_store_cur(&storage->latest_seq,     0u);  /* seqlock 序列计数：偶=稳定初态 */
    storage->owner_cpu         = cfg->owner_cpu;
    storage->frozen            = 0u;
    storage->write_in_progress = 0u;
    storage->reader_count      = 0u;
    /* BLOCK 后端字段：open 时清零，bind 后写入，之后只读 */
    storage->block_iface       = NULL;
    storage->block_ctx         = NULL;
    for (i = 0u; i < storage->max_consumers; i++) {
        bus_store_cur(&storage->readers[i].read_cur, 0u);
        storage->readers[i].overflow_count = 0u;
        storage->readers[i].attached       = 0u;
    }
    h->storage = storage;
    BM_LOGD("bus", "open mode=%u cap=%u", (unsigned)storage->mode,
            (unsigned)storage->capacity);
    return BM_OK;
}

/**
 * @brief 释放 bus 句柄（清空指针，不释放内存，存储为静态）
 *
 * @param h 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID 句柄空指针
 */
int bm_bus_close(bm_bus_t *h) {
    if (!h) {
        return BM_ERR_INVALID;
    }
    h->storage = NULL;
    return BM_OK;
}

/**
 * @brief 冻结总线拓扑，freeze 后 reader_attach 返回 BM_ERR_BUSY
 *
 * 建议在所有核完成 attach、IRQ release 之后、稳态主循环之前调用。
 * 与 bm_event_freeze_subscriptions() 同构。幂等（重复 freeze 无副作用）。
 *
 * @param h bus 句柄
 * @return BM_OK 成功（含幂等重复调用）；BM_ERR_INVALID 句柄无效
 */
int bm_bus_freeze(bm_bus_t *h) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;

    if (!h || !h->storage) {
        return BM_ERR_INVALID;
    }
    st = h->storage;
    BUS_LOCK(&s);
    st->frozen = 1u;
    BUS_UNLOCK(s);
    BM_LOGD("bus", "frozen mode=%u readers=%u",
            (unsigned)st->mode, (unsigned)st->reader_count);
    return BM_OK;
}

/**
 * @brief 将 bus storage 运行期状态复位到 open 后 pristine 并解冻（frozen=0）
 *
 * 与 bm_event_reset() 语义对称：清零所有运行期游标/计数/reader slot，
 * 置 frozen=0，使 bus 可重新执行 reader_attach/bind/freeze 流程。
 * 编译期/open 配置（mode/capacity/elem_size/max_consumers/owner_cpu/data_buf/readers/
 * block_iface/block_ctx）保持不变。
 *
 * @note BLOCK 后端不随 reset 自动复位，后端有独立生命周期，调用方须自行处理。
 * @note 多核契约：仅允许在安全相位（单一协调流程、无并发产消）调用，框架不强制。
 * @note 幂等：连续多次 reset 无副作用。
 *
 * @param h bus 句柄指针
 * @return BM_OK 成功；BM_ERR_INVALID h 或 h->storage 为空
 */
int bm_bus_reset(bm_bus_t *h) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;
    uint32_t i;

    if (!h || !h->storage) {
        return BM_ERR_INVALID;
    }
    st = h->storage;

    BUS_LOCK(&s);

    /* --- 写游标与 LATEST 三缓冲标记 --- */
    bus_store_cur(&st->write_cur,        0u);
    bus_store_cur(&st->latest_published, BM_BUS_LATEST_NONE);
    bus_store_cur(&st->latest_reading,   BM_BUS_LATEST_NONE);
    bus_store_cur(&st->latest_writing,   0u);
    bus_store_cur(&st->latest_seq,       0u); /* seqlock 序列计数归零 */

    /* --- 进行中的写操作 --- */
    st->write_in_progress = 0u;

    /* --- 读者槽 --- */
    st->reader_count = 0u;
    for (i = 0u; i < st->max_consumers; i++) {
        bus_store_cur(&st->readers[i].read_cur, 0u);
        st->readers[i].overflow_count = 0u;
        st->readers[i].attached       = 0u;
    }

    /* --- 解冻：置 frozen=0，与 bm_event_reset 解冻订阅表的语义对称 --- */
    st->frozen = 0u;

    BUS_UNLOCK(s);

    BM_LOGD("bus", "reset mode=%u cap=%u", (unsigned)st->mode,
            (unsigned)st->capacity);
    return BM_OK;
}

/**
 * @brief 结构性校验（委托 bus_storage_valid）：cap>=2、LATEST cap>=3、mode 合法、
 *        QUEUE max_consumers==1
 *
 * @param h 句柄指针
 * @return BM_OK 通过；BM_ERR_INVALID 不通过
 */
int bm_bus_validate(const bm_bus_t *h) {
    if (!h || !h->storage) {
        return BM_ERR_INVALID;
    }
    return bus_storage_valid(h->storage);
}

/* ------------------------------------------------------------------ */
/* acquire_write / commit / abort                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief LATEST 模式选写槽（三缓冲选槽，与 bm_snapshot_choose_buffer 等价）
 *
 * 避开 published（读者可能正取）与 reading（读者已登记正读）两槽，
 * 防多核读写撕裂。cap>=3 时必有第三槽可用。published 或 reading 可能为
 * BM_BUS_LATEST_NONE（0xFFFFFFFF），视为"无此槽"，不参与避开。
 *
 * @param published 当前已发布槽索引（或 BM_BUS_LATEST_NONE）
 * @param reading   读者正在读的槽索引（或 BM_BUS_LATEST_NONE 表示无读者）
 * @param cap       环容量（建议 3，最小 2）
 * @return 可写入的槽索引（0..cap-1）
 */
static uint32_t bus_latest_choose_write_slot(uint32_t published,
                                             uint32_t reading,
                                             uint32_t cap)
{
    uint32_t i;

    for (i = 0u; i < cap; i++) {
        if (i != published && i != reading) {
            return i;
        }
    }
    /* cap==2 时无第三槽，退化：覆盖非 reading 槽（双缓冲，多核建议 cap>=3） */
    return (published < cap && published != reading) ?
           published : ((published + 1u) % cap);
}

/**
 * @brief QUEUE 满判据：最慢（唯一）读者落后达 cap-1 即视为满
 *
 * 保留一槽区分满/空：实际可存 cap-1 项。QUEUE max_consumers==1，
 * 只有一个读者 readers[0]；未 attach 时其 read_cur 为 0（DEFINE 初值），
 * 写游标自 0 推进，至多积累 cap-1 项后判满，不会越界覆盖。
 *
 * @param st storage 指针
 * @return 非 0 表示已满
 */
static int bus_queue_is_full(const bm_bus_storage_t *st) {
    uint32_t wc = bus_load_cur(&st->write_cur);
    uint32_t rc = bus_load_cur(&st->readers[0].read_cur);

    return (wc - rc) >= (st->capacity - 1u);
}

/**
 * @brief 借出下一写槽（零拷贝；QUEUE/SIGNAL）
 *
 * 写路径 O(1) 无阻塞：用 write_in_progress 防重入。
 * QUEUE：满则立即返回 BM_ERR_OVERFLOW 拒绝（不给槽、不推进、不覆盖）。
 * SIGNAL：永不拒绝，直接给当前 write_cur 槽，覆盖最旧（lap 由读者检测）。
 * LATEST：三缓冲 choose 避开 published/reading 两槽选写槽。
 * BLOCK：Phase 2，返回 BM_ERR_NOT_SUPPORTED。
 *
 * owner-only：写路径仅允许 owner_cpu 调用（契约见头文件，框架不强制）。
 *
 * @param h        bus 句柄
 * @param slot_out 输出：写槽指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数错；BM_ERR_BUSY 上次借还未结束；
 *         BM_ERR_OVERFLOW QUEUE 满拒绝；BM_ERR_NOT_SUPPORTED BLOCK（Phase 2）
 */
int bm_bus_acquire_write(bm_bus_t *h, void **slot_out) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;

    if (!h || !h->storage || !slot_out) {
        return BM_ERR_INVALID;
    }
    st = h->storage;

    if (st->mode == BM_BUS_BLOCK) {
        return BM_ERR_NOT_SUPPORTED;
    }
    /* 写路径 owner 守卫：多核下拒绝非 owner 核写入，单核 no-op（bm_cpu_is_owner 编译期内联）。
     * 对应 bm_bus.h §写路径契约：多核由 bm_cpu_is_owner 强制（单核 no-op）。 */
    if (!bm_cpu_is_owner(st->owner_cpu)) {
        return BM_ERR_INVALID;
    }
    if (st->mode == BM_BUS_LATEST) {
        /* LATEST 三缓冲：choose 避开 published 与 reading 两槽，选出写槽存入
         * latest_writing（scratch），commit 时以 release 序拷入 latest_published。
         * 完全不复用 write_cur。write_in_progress 防重入。 */
        uint32_t pub, reading, wslot;

        BUS_LOCK(&s);
        if (st->write_in_progress) {
            BUS_UNLOCK(s);
            return BM_ERR_BUSY;
        }
        pub     = bus_load_cur(&st->latest_published);
        reading = bus_load_cur(&st->latest_reading);
        wslot   = bus_latest_choose_write_slot(pub, reading, st->capacity);
        st->write_in_progress = 1u;
        /* 选中槽存入 latest_writing（scratch），commit 时拷入 latest_published */
        bus_store_cur(&st->latest_writing, wslot);
        *slot_out = st->data_buf + wslot * st->elem_size;
        BUS_UNLOCK(s);
        return BM_OK;
    }

    BUS_LOCK(&s);
    if (st->write_in_progress) {
        BUS_UNLOCK(s);
        return BM_ERR_BUSY;
    }
    /* QUEUE 满则拒绝，不覆盖未读（不丢语义） */
    if (st->mode == BM_BUS_QUEUE && bus_queue_is_full(st)) {
        BUS_UNLOCK(s);
        return BM_ERR_OVERFLOW;
    }
    st->write_in_progress = 1u;
    *slot_out = bus_slot_ptr(st, bus_load_cur(&st->write_cur));
    BUS_UNLOCK(s);
    return BM_OK;
}

/**
 * @brief 提交写操作：以 release 内存序发布数据，推进 write_cur（QUEUE/SIGNAL）
 *
 * LATEST：以 release 序把 latest_writing 写槽拷入 latest_published 发布。
 * BLOCK：Phase 2，返回 BM_ERR_NOT_SUPPORTED。owner-only（见头文件契约）。
 *
 * @param h bus 句柄
 * @return BM_OK 成功；BM_ERR_INVALID 未 acquire；BM_ERR_NOT_SUPPORTED BLOCK（Phase 2）
 */
int bm_bus_commit(bm_bus_t *h) {
    bm_bus_storage_t *st;
    uint32_t cur;
    bm_irq_state_t s;

    if (!h || !h->storage) {
        return BM_ERR_INVALID;
    }
    st = h->storage;

    if (st->mode == BM_BUS_BLOCK) {
        return BM_ERR_NOT_SUPPORTED;
    }
    /* 写路径 owner 守卫：与 acquire_write 对称，多核下确保提交方仍为 owner 核。 */
    if (!bm_cpu_is_owner(st->owner_cpu)) {
        return BM_ERR_INVALID;
    }
    if (st->mode == BM_BUS_LATEST) {
        /* LATEST 三缓冲 commit：以 release 序发布 latest_writing 写槽，
         * 同时以 seqlock 屏障保护 bm_bus_latest_read 多观察者拷出路径。
         * 数据写入先于 latest_published 可见，读者 acquire 序读游标后能看到数据。 */
        uint32_t wslot;
        uint32_t seq;

        BUS_LOCK(&s);
        if (!st->write_in_progress) {
            BUS_UNLOCK(s);
            return BM_ERR_INVALID;
        }
        wslot = bus_load_cur(&st->latest_writing);   /* acquire_write 时已选好 */
        /* seqlock 写侧屏障：奇=写进行中 → 发布数据 → 偶=稳定。
         * 两次 release store 保证：多核读者先看到奇才会重试，待数据完全
         * 写入后读者看到偶且前后相等才认为拷贝有效（DET-01 seqlock 扩展）。
         * 单写者在 BUS_LOCK 保护下，无需 CAS，直接 store。 */
        seq = bus_load_cur(&st->latest_seq);
        bus_store_cur(&st->latest_seq, seq + 1u);    /* 奇：写进行中 */
        /* release 序发布：数据先于 latest_published 可见，读者 acquire 序读到 */
        bus_store_cur(&st->latest_published, wslot);
        bus_store_cur(&st->latest_seq, seq + 2u);    /* 偶：写完成、seqlock 稳态 */
        st->write_in_progress = 0u;
        BUS_UNLOCK(s);
        return BM_OK;
    }

    BUS_LOCK(&s);
    if (!st->write_in_progress) {
        BUS_UNLOCK(s);
        return BM_ERR_INVALID;
    }
    /* QUEUE/SIGNAL：推进写游标（release 序由 bus_store_cur 封装）；
     * 写入的数据先于游标可见，读者 acquire 序读游标后能看到数据 */
    cur = bus_load_cur(&st->write_cur);
    bus_store_cur(&st->write_cur, cur + 1u);
    st->write_in_progress = 0u;
    BUS_UNLOCK(s);
    return BM_OK;
}

/**
 * @brief 放弃当前写操作（不推进写游标）
 *
 * @param h bus 句柄
 * @return BM_OK 成功；BM_ERR_INVALID 未 acquire
 */
int bm_bus_abort(bm_bus_t *h) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;

    if (!h || !h->storage) {
        return BM_ERR_INVALID;
    }
    st = h->storage;
    /* 写路径 owner 守卫：abort 属写路径，与 acquire_write/commit 约束对称。 */
    if (!bm_cpu_is_owner(st->owner_cpu)) {
        return BM_ERR_INVALID;
    }
    BUS_LOCK(&s);
    if (!st->write_in_progress) {
        BUS_UNLOCK(s);
        return BM_ERR_INVALID;
    }
    st->write_in_progress = 0u;
    BUS_UNLOCK(s);
    return BM_OK;
}

/* ------------------------------------------------------------------ */
/* 读者接口实现（reader_attach / acquire_read / release）               */
/* ------------------------------------------------------------------ */

/**
 * @brief 登记读者，分配追赶游标
 *
 * freeze 后拒绝（BM_ERR_BUSY）；超 max_consumers 返回 BM_ERR_INVALID。
 * LATEST：恒成功，不占 max_consumers 配额，slot_idx 设 UINT32_MAX。
 * QUEUE：附加校验当前读者数 == 0（唯一消费者），超 1 返回 BM_ERR_INVALID。
 *
 * @note 并发约束（多核）：reader_attach 修改 reader_count 与扫描 readers[]，
 *       多核路径下 BUS_LOCK 为 no-op，故 reader_attach 仅允许在 freeze 之前、
 *       由单一协调流程串行调用，不得在多核并发路径或 IRQ 中调用。
 *
 * @param h  bus 句柄
 * @param r  读者句柄（输出）
 * @return BM_OK 成功；负值表示失败
 */
int bm_bus_reader_attach(bm_bus_t *h, bm_bus_reader_t *r) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;
    uint32_t i;

    if (!h || !h->storage || !r) {
        return BM_ERR_INVALID;
    }
    st = h->storage;

    if (st->mode == BM_BUS_BLOCK) {
        return BM_ERR_NOT_SUPPORTED;
    }

    BUS_LOCK(&s);
    if (st->frozen) {
        BUS_UNLOCK(s);
        return BM_ERR_BUSY;
    }

    if (st->mode == BM_BUS_LATEST) {
        /* LATEST：单读者 SPSC 约束——reader_count 追踪是否已有读者 attach。
         * reader_count == 0 表示空闲可 attach，否则已有读者拒绝（返回 BM_ERR_INVALID）。
         * LATEST 不持游标，不占 max_consumers 配额，但 reader_count 仍递增以追踪唯一性。 */
        if (st->reader_count >= 1u) {
            BUS_UNLOCK(s);
            return BM_ERR_INVALID;
        }
        st->reader_count++;
        r->storage  = st;
        r->slot_idx = UINT32_MAX;
        BUS_UNLOCK(s);
        return BM_OK;
    }

    /* QUEUE：最多 1 个读者（唯一消费者语义）*/
    if (st->mode == BM_BUS_QUEUE && st->reader_count >= 1u) {
        BUS_UNLOCK(s);
        return BM_ERR_INVALID;
    }
    /* SIGNAL/QUEUE 通用上界：不超 max_consumers */
    if (st->reader_count >= st->max_consumers) {
        BUS_UNLOCK(s);
        return BM_ERR_INVALID;
    }

    /* 找空闲槽 */
    for (i = 0u; i < st->max_consumers; i++) {
        if (!st->readers[i].attached) {
            break;
        }
    }
    if (i >= st->max_consumers) {
        BUS_UNLOCK(s);
        return BM_ERR_INVALID;
    }

    /* 初始化游标指向当前 write_cur（不接收历史数据）*/
    bus_store_cur(&st->readers[i].read_cur,
                  bus_load_cur(&st->write_cur));
    st->readers[i].overflow_count = 0u;
    st->readers[i].attached       = 1u;
    st->reader_count++;
    r->storage  = st;
    r->slot_idx = i;
    BUS_UNLOCK(s);
    BM_LOGD("bus", "reader_attach slot=%u mode=%u",
            (unsigned)i, (unsigned)st->mode);
    return BM_OK;
}

/**
 * @brief 借出当前可读槽（零拷贝）
 *
 * QUEUE/SIGNAL：以 acquire 内存序先读 write_cur，再读数据，
 *   确保读到写者已 commit 发布的内容（多核内存序）。
 * LATEST：以 acquire 序读 latest_published，将该槽写入 latest_reading 标记
 *   （供写者 choose 避开，防多核读写撕裂），返回该槽指针；release 清标记。
 * SIGNAL lap 检测：(write_cur - read_cur) >= cap 表示读者被绕过，
 *   跳到 write_cur - (cap-1)（最旧可用槽），返回 BM_ERR_OVERFLOW。
 *   QUEUE 因写端满即拒绝（不丢），读者永不被绕过，该分支对 QUEUE 不触发，
 *   但保留以共享一份代码（QUEUE/SIGNAL 同构）。
 *
 * @param r        读者句柄
 * @param slot_out 输出：读槽指针（const，不可写）
 * @return BM_OK / BM_ERR_WOULD_BLOCK / BM_ERR_OVERFLOW / BM_ERR_INVALID
 */
int bm_bus_acquire_read(bm_bus_reader_t *r, const void **slot_out) {
    bm_bus_storage_t *st;
    uint32_t wc, rc;
    bm_irq_state_t s;

    if (!r || !r->storage || !slot_out) {
        return BM_ERR_INVALID;
    }
    st = r->storage;

    if (st->mode == BM_BUS_LATEST) {
        /* LATEST：spin-until-stable 循环（照搬 bm_snapshot READ 逻辑）。
         * 先读 latest_published（acquire 序），登记 latest_reading（release 序）
         * 防写者 choose_slot 覆盖正读槽；再次 acquire 读 latest_published 确认
         * 期间未变，变则重试；重试至多 BM_CONFIG_BUS_LATEST_MAX_RETRIES 次，
         * 写者持续抢占致超界则非阻塞返回 BM_ERR_WOULD_BLOCK（DET-01，WCET 有界）。
         * 若 published == BM_BUS_LATEST_NONE（无数据），返回 BM_ERR_WOULD_BLOCK。
         *
         * 并发约束：本路径读者不持 BUS_LOCK，属 SPSC 单读者无锁设计
         * （对齐 bm_snapshot READ）。仅在 reader_attach 强制的单读者契约下
         * 无读读竞争；若未来扩展为多读者，latest_reading 单一标记无法区分
         * 多个读者正读槽，须重新设计（如每读者独立 reading 标记）。 */
        uint32_t p;
        uint32_t retry = 0u;

        for (;;) {
            p = bus_load_cur(&st->latest_published);
            if (p == BM_BUS_LATEST_NONE) {
                /* 防御：无数据早返回前清 reading 标记，避免未来 reset/close
                 * 等路径残留陈旧标记（与 release 语义一致） */
                bus_store_cur(&st->latest_reading, BM_BUS_LATEST_NONE);
                return BM_ERR_WOULD_BLOCK;
            }
            bus_store_cur(&st->latest_reading, p);
#if defined(BM_ENABLE_BUS_TEST_HOOK)
            /* 测试缝：模拟写者在读窗口持续发布，强制下方再读失稳；
             * 生产构建不编入，零开销（用于 DET-01 重试上界验证）。 */
            if (bm_bus_test_latest_read_hook != NULL) {
                bm_bus_test_latest_read_hook(st);
            }
#endif
            if (bus_load_cur(&st->latest_published) == p) {
                break; /* 期间未变，读槽稳定 */
            }
            /* DET-01：写者在读窗口内覆盖发布则重试，但给重试封顶——
             * 避免写者持续抢占时无界自旋拖垮 acquire_read 的 WCET 可分析性。
             * 超界视同"暂无稳定快照"，清 reading 标记后非阻塞返回，
             * 调用方下一 tick 重取（与 BM_ERR_WOULD_BLOCK 语义一致）。 */
            if (++retry >= BM_CONFIG_BUS_LATEST_MAX_RETRIES) {
                bus_store_cur(&st->latest_reading, BM_BUS_LATEST_NONE);
                return BM_ERR_WOULD_BLOCK;
            }
        }

        *slot_out = st->data_buf + (size_t)p * st->elem_size;
        return BM_OK;
    }

    /* QUEUE / SIGNAL */
    if (r->slot_idx >= st->max_consumers) {
        return BM_ERR_INVALID;
    }

    BUS_LOCK(&s);
    wc = bus_load_cur(&st->write_cur);
    rc = bus_load_cur(&st->readers[r->slot_idx].read_cur);

    if (wc == rc) {
        /* 无新数据 */
        BUS_UNLOCK(s);
        return BM_ERR_WOULD_BLOCK;
    }

    /* SIGNAL lap 检测：差值 >= capacity 表示读者被绕过 */
    if ((wc - rc) >= st->capacity) {
        /* 跳到最旧可用槽（write_cur - (cap-1)）*/
        uint32_t new_rc = wc - (st->capacity - 1u);
        st->readers[r->slot_idx].overflow_count =
            bm_u32_saturating_inc(st->readers[r->slot_idx].overflow_count);
        bus_store_cur(&st->readers[r->slot_idx].read_cur, new_rc);
        rc = new_rc;
        BUS_UNLOCK(s);
        *slot_out = bus_slot_ptr(st, rc);
        return BM_ERR_OVERFLOW;
    }

    *slot_out = bus_slot_ptr(st, rc);
    BUS_UNLOCK(s);
    return BM_OK;
}

/**
 * @brief 归还读槽；QUEUE/SIGNAL 推进读者游标（含消费窗口复检），LATEST 清 reading 标记
 *
 * LATEST：将 latest_reading 清为 BM_BUS_LATEST_NONE，使写者 choose 可重新
 *   使用该槽（不再是纯空操作）；恒返回 BM_OK。
 *
 * QUEUE/SIGNAL **消费窗口复检（确定性零拷贝借用保护）**：acquire_read 借出槽
 *   指针后，调用方在 release 之前读取该槽，这段消费窗口内 SIGNAL 写者可能覆盖式
 *   推进并绕过本槽。release 重读 write_cur 与本读者 read_cur：若 (wc-rc)>=cap，
 *   说明写者在窗口内已写到/越过本槽，本帧已撕裂——递增 overflow_count、把游标跳到
 *   最旧可用槽 wc-(cap-1)，返回 **BM_ERR_OVERFLOW** 通知调用方**作废本帧并重取**。
 *   与 acquire_read 的 lap 检测同构：acquire 挡"读前已被绕过"，release 挡"读中
 *   被绕过"，两端对称。
 *   - **QUEUE**：写者满即拒（不覆盖未读），(wc-rc) 永 < cap，复检分支永不触发，
 *     release 恒返回 BM_OK。
 *   - **LATEST**：借用窗口由 reading 标记（写者 choose 避让）保护，无需复检。
 *
 * @par 多核内存序
 * 复检以 acquire 序读 write_cur：写者 commit 时以 release 序推进 write_cur（数据
 * 写先于游标可见），故 release 读到的 wc 是写者进度的可靠下界；本槽若真被覆盖必有
 * wc-rc>=cap+1，连"正在写未提交"的 wc-rc==cap 也落入复检，无漏判（仅边界保守误判）。
 *
 * @param r 读者句柄
 * @return BM_OK 成功；BM_ERR_OVERFLOW SIGNAL 消费窗口内本帧被覆盖（作废重取）；
 *         BM_ERR_INVALID 参数错（slot_idx 越界）
 */
int bm_bus_release(bm_bus_reader_t *r) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;

    if (!r || !r->storage) {
        return BM_ERR_INVALID;
    }
    st = r->storage;

    if (st->mode == BM_BUS_LATEST) {
        /* 清 reading 标记，释放该槽供写者复用 */
        bus_store_cur(&st->latest_reading, BM_BUS_LATEST_NONE);
        return BM_OK;
    }
    if (r->slot_idx >= st->max_consumers) {
        return BM_ERR_INVALID;
    }

    BUS_LOCK(&s);
    {
        uint32_t wc = bus_load_cur(&st->write_cur);
        uint32_t rc = bus_load_cur(&st->readers[r->slot_idx].read_cur);

        /* 消费窗口复检：借出后写者推进达 cap，本槽已被覆盖，本帧撕裂。
         * QUEUE 满即拒，(wc-rc) 永 < cap，此分支恒不触发。 */
        if ((wc - rc) >= st->capacity) {
            uint32_t new_rc = wc - (st->capacity - 1u);
            st->readers[r->slot_idx].overflow_count =
                bm_u32_saturating_inc(st->readers[r->slot_idx].overflow_count);
            bus_store_cur(&st->readers[r->slot_idx].read_cur, new_rc);
            BUS_UNLOCK(s);
            return BM_ERR_OVERFLOW;
        }
        bus_store_cur(&st->readers[r->slot_idx].read_cur, rc + 1u);
    }
    BUS_UNLOCK(s);
    return BM_OK;
}

/**
 * @brief 查询当前读者可消费的元素数量
 *
 * LATEST：有已发布数据（latest_published != NONE）返回 1，否则 0。
 * QUEUE/SIGNAL：返回 write_cur - read_cur，超 cap-1 则裁剪为 cap-1（overflow 场景）。
 *
 * @param r 读者句柄
 * @return 可读元素数；句柄无效返回 0
 */
uint32_t bm_bus_ready_count(const bm_bus_reader_t *r) {
    const bm_bus_storage_t *st;
    uint32_t wc, rc, diff;

    if (!r || !r->storage) {
        return 0u;
    }
    st = r->storage;
    if (st->mode == BM_BUS_LATEST) {
        /* LATEST：有发布数据（latest_published != BM_BUS_LATEST_NONE）返回 1，否则 0 */
        return (bus_load_cur(&st->latest_published) != BM_BUS_LATEST_NONE) ? 1u : 0u;
    }
    if (r->slot_idx >= st->max_consumers) {
        return 0u;
    }
    wc   = bus_load_cur(&st->write_cur);
    rc   = bus_load_cur(&st->readers[r->slot_idx].read_cur);
    diff = wc - rc;
    /* 超 cap 说明发生 overflow，裁剪为 cap-1（保守可用估计） */
    return (diff > st->capacity - 1u) ? (st->capacity - 1u) : diff;
}

/**
 * @brief 获取写侧统计快照
 *
 * @note write_count 取自 write_cur，仅对 QUEUE/SIGNAL 有意义；**LATEST 不使用
 *       write_cur**（走 latest_published 三缓冲），其 write_count 恒为 0，不反映发布次数。
 *       BLOCK 模式 write_count/overflow_count 均为 0（统计由后端持有）。
 *
 * @param h   bus 句柄
 * @param out 输出统计结构
 * @return BM_OK 成功；BM_ERR_INVALID 参数错
 */
int bm_bus_stats(const bm_bus_t *h, bm_bus_stats_t *out) {
    /* write_count = write_cur（QUEUE/SIGNAL；LATEST/BLOCK 恒 0） */
    if (!h || !h->storage || !out) {
        return BM_ERR_INVALID;
    }
    out->write_count    = bus_load_cur(&h->storage->write_cur);
    out->overflow_count = 0u;  /* 写侧 overflow 计数字段暂未内置；读者侧 overflow 已存于
                                 * readers[i].overflow_count；QUEUE 写满拒绝次数由调用方
                                 * 累加 acquire_write 返回 BM_ERR_OVERFLOW 的次数 */
    return BM_OK;
}

/* ------------------------------------------------------------------ */
/* BLOCK 模式专用接口实现                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief 绑定 BLOCK 后端（仅 BLOCK 模式合法，必须在 freeze 之前调用）
 *
 * 将 vtable 指针与上下文写入 storage->block_iface/block_ctx。freeze 后拒绝绑定。
 * 重复 bind（未 freeze 前）覆盖旧绑定，以支持 open→bind→open→bind 的重置场景。
 *
 * @param h     bus 句柄
 * @param iface 后端 vtable 指针
 * @param ctx   后端上下文
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_BUSY 已 freeze；
 *         BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_bind_block_backend(bm_bus_t *h,
                               const bm_block_backend_iface_t *iface,
                               void *ctx) {
    bm_bus_storage_t *st;
    bm_irq_state_t s;

    if (!h || !h->storage || !iface) {
        return BM_ERR_INVALID;
    }
    st = h->storage;

    if (st->mode != BM_BUS_BLOCK) {
        return BM_ERR_NOT_SUPPORTED;
    }

    BUS_LOCK(&s);
    if (st->frozen) {
        BUS_UNLOCK(s);
        return BM_ERR_BUSY;
    }
    st->block_iface = iface;
    st->block_ctx   = ctx;
    BUS_UNLOCK(s);

    BM_LOGD("bus", "block_backend bound mode=BLOCK owner_cpu=%u",
            (unsigned)st->owner_cpu);
    return BM_OK;
}

/**
 * @brief 检查 BLOCK 后端已就绪（已绑定 + 非 BLOCK 检测）的公共前置校验
 *
 * @param h  bus 句柄
 * @param st 输出：storage 指针（仅成功时有效）
 * @return BM_OK 通过；BM_ERR_NOT_SUPPORTED 非 BLOCK；BM_ERR_INVALID 未绑定或参数错
 */
static int bus_block_check(const bm_bus_t *h, bm_bus_storage_t **st) {
    if (!h || !h->storage) {
        return BM_ERR_INVALID;
    }
    *st = h->storage;
    if ((*st)->mode != BM_BUS_BLOCK) {
        return BM_ERR_NOT_SUPPORTED;
    }
    if (!(*st)->block_iface) {
        /* 后端未绑定：返回 BM_ERR_INVALID，与"参数非法"语义一致 */
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief BLOCK 生产者：借出空闲块，透传至后端 producer_acquire
 *
 * @param h         bus 句柄
 * @param block_out 输出：不透明 block 指针
 * @return BM_OK 成功；BM_ERR_OVERFLOW 无空闲块；BM_ERR_INVALID 未绑定或参数非法；
 *         BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_produce_acquire(bm_bus_t *h, void **block_out) {
    bm_bus_storage_t *st;
    int rc;

    if (!block_out) {
        return BM_ERR_INVALID;
    }
    rc = bus_block_check(h, &st);
    if (rc != BM_OK) {
        return rc;
    }
    return st->block_iface->producer_acquire(st->block_ctx, block_out);
}

/**
 * @brief BLOCK 生产者：提交块，透传至后端 producer_commit
 *
 * @param h           bus 句柄
 * @param block       由 bm_bus_block_produce_acquire 借出的块
 * @param valid_bytes 有效数据字节数
 * @param ts_ns       时间戳（纳秒），0 表示无时间戳
 * @return BM_OK 成功；BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_produce_commit(bm_bus_t *h, void *block,
                                uint32_t valid_bytes, uint64_t ts_ns) {
    bm_bus_storage_t *st;
    int rc;

    if (!block) {
        return BM_ERR_INVALID;
    }
    rc = bus_block_check(h, &st);
    if (rc != BM_OK) {
        return rc;
    }
    return st->block_iface->producer_commit(st->block_ctx, block,
                                             valid_bytes, ts_ns);
}

/**
 * @brief BLOCK 生产者：放弃已借块，透传至后端 producer_abort
 *
 * @param h     bus 句柄
 * @param block 由 bm_bus_block_produce_acquire 借出的块
 * @return BM_OK 成功；BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_produce_abort(bm_bus_t *h, void *block) {
    bm_bus_storage_t *st;
    int rc;

    if (!block) {
        return BM_ERR_INVALID;
    }
    rc = bus_block_check(h, &st);
    if (rc != BM_OK) {
        return rc;
    }
    return st->block_iface->producer_abort(st->block_ctx, block);
}

/**
 * @brief BLOCK 消费者：借出最旧 READY 块，透传至后端 consumer_acquire
 *
 * @param h         bus 句柄
 * @param block_out 输出：不透明 block 指针
 * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 无 READY 块；
 *         BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_consume_acquire(bm_bus_t *h, void **block_out) {
    bm_bus_storage_t *st;
    int rc;

    if (!block_out) {
        return BM_ERR_INVALID;
    }
    rc = bus_block_check(h, &st);
    if (rc != BM_OK) {
        return rc;
    }
    return st->block_iface->consumer_acquire(st->block_ctx, block_out);
}

/**
 * @brief BLOCK 消费者：归还块，透传至后端 consumer_release
 *
 * @param h     bus 句柄
 * @param block 由 bm_bus_block_consume_acquire 借出的块
 * @return BM_OK 成功；BM_ERR_INVALID 未绑定或参数非法；BM_ERR_NOT_SUPPORTED 非 BLOCK 模式
 */
int bm_bus_block_consume_release(bm_bus_t *h, void *block) {
    bm_bus_storage_t *st;
    int rc;

    if (!block) {
        return BM_ERR_INVALID;
    }
    rc = bus_block_check(h, &st);
    if (rc != BM_OK) {
        return rc;
    }
    return st->block_iface->consumer_release(st->block_ctx, block);
}

/* ------------------------------------------------------------------ */
/* LATEST 多观察者拷出式读（seqlock 增量并存方案）                     */
/* ------------------------------------------------------------------ */

/**
 * @brief LATEST 多观察者拷出式读（seqlock + 有界重试，非阻塞）
 *
 * 以 seqlock（latest_seq）保证多核下拷贝一致性：
 *   读 seq1（偶=稳定）→ 读 latest_published → memcpy → 读 seq2；
 *   seq1==seq2 且均为偶则拷贝有效（写者未在窗口内发布），否则重试；
 *   重试上界 BM_CONFIG_BUS_LATEST_MAX_RETRIES，超界返回 BM_ERR_WOULD_BLOCK
 *   （非阻塞，WCET 可静态分析，DET-01 seqlock 扩展）。
 *
 * 不使用 latest_reading，与单读者零拷贝借还路径（acquire_read/release）
 * 完全解耦：两种读法可在同一 bus 上并存互不干扰。
 *
 * @param h   bus 句柄指针（只读，不修改运行期状态）
 * @param dst 目标缓冲区（调用者分配，大小须 >= storage->elem_size）
 * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 无数据或重试耗尽；
 *         BM_ERR_NOT_SUPPORTED 非 LATEST；BM_ERR_INVALID 参数非法
 */
int bm_bus_latest_read(const bm_bus_t *h, void *dst) {
    bm_bus_storage_t *st;
    uint32_t seq1, seq2, p;
    uint32_t retry = 0u;

    if (!h || !h->storage || !dst) {
        return BM_ERR_INVALID;
    }
    st = h->storage;

    if (st->mode != BM_BUS_LATEST) {
        return BM_ERR_NOT_SUPPORTED;
    }

    for (;;) {
        /* seqlock 读侧第一步：读序号，奇数表示写者正在发布，需等写者完成后重试 */
        seq1 = bus_load_cur(&st->latest_seq);
        if (seq1 & 1u) {
            /* 写者持有写中状态，重试计数器递增 */
            if (++retry >= BM_CONFIG_BUS_LATEST_MAX_RETRIES) {
                return BM_ERR_WOULD_BLOCK;
            }
            continue;
        }

        /* 读最新发布槽索引 */
        p = bus_load_cur(&st->latest_published);
        if (p == BM_BUS_LATEST_NONE) {
            /* open/reset 后尚无 commit，无有效数据 */
            return BM_ERR_WOULD_BLOCK;
        }

        /* 拷出：从发布槽复制 elem_size 字节到调用者缓冲区 */
        (void)memcpy(dst, st->data_buf + (size_t)p * st->elem_size, st->elem_size);

#if defined(BM_ENABLE_BUS_TEST_HOOK)
        /* 测试缝：模拟写者在拷贝完成后推进 seq，强制 seqlock 失稳触发重试；
         * 生产构建不编入，零开销（DET-01 seqlock 扩展重试上界验证）。 */
        if (bm_bus_test_latest_multi_read_hook != NULL) {
            bm_bus_test_latest_multi_read_hook(st);
        }
#endif

        /* seqlock 读侧第二步：复读序号，与 seq1 相等说明拷贝期间无写者发布 */
        seq2 = bus_load_cur(&st->latest_seq);
        if (seq1 == seq2) {
            return BM_OK; /* seqlock 验证通过：数据一致 */
        }

        /* seqlock 失稳：写者在拷贝期间发布新值，重试 */
        if (++retry >= BM_CONFIG_BUS_LATEST_MAX_RETRIES) {
            return BM_ERR_WOULD_BLOCK;
        }
    }
}

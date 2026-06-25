/**
 * @file bm_bus.c
 * @brief bm_bus 统一数据总线实现（Phase 1：SPMC 有界环后端）
 *
 * LATEST / QUEUE / SIGNAL 三种 mode 共用同一套借还逻辑，并发层按
 * BM_CONFIG_CPU_COUNT 切换。零动态分配，写者 O(1) 无阻塞。
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       0.1            zeh            Phase 1 Task 2 写路径骨架
 * 2026-06-25       0.2            zeh            Phase 1 Task 3 QUEUE 读路径：reader_attach 修正、acquire_read、release、freeze
 *
 */
#include "bm/core/bm_bus.h"
#include "bm_critical_wrap.h"
#include "bm_log.h"
#include "bm_safety.h"

#include <string.h>

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
    /* LATEST 三缓冲多核防撕裂：需第三槽，强制 cap>=3 */
    if (st->mode == BM_BUS_LATEST && st->capacity < 3u) {
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
    bus_store_cur(&storage->latest_published, 0u);
    bus_store_cur(&storage->latest_reading, BM_BUS_LATEST_NONE);
    bus_store_cur(&storage->latest_writing, 0u);
    storage->owner_cpu         = cfg->owner_cpu;
    storage->frozen            = 0u;
    storage->write_in_progress = 0u;
    storage->reader_count      = 0u;
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
 * @brief 结构性校验：capacity >= 2，max_consumers 上界，mode 合法，owner 已设
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
 * LATEST：本 Task 占位返回 BM_ERR_NOT_SUPPORTED，Task 5 替换为三缓冲分支。
 *
 * @param h        bus 句柄
 * @param slot_out 输出：写槽指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数错；BM_ERR_BUSY 上次借还未结束；
 *         BM_ERR_OVERFLOW QUEUE 满拒绝；BM_ERR_NOT_SUPPORTED BLOCK（及 Task 5 前的 LATEST 占位）
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
    if (st->mode == BM_BUS_LATEST) {
        /* 占位：Task 5 Step 5.4 替换为三缓冲 choose 分支 */
        return BM_ERR_NOT_SUPPORTED;
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
 * LATEST：本 Task 占位返回 BM_ERR_NOT_SUPPORTED，Task 5 替换为 latest_published 分支。
 *
 * @param h bus 句柄
 * @return BM_OK 成功；BM_ERR_INVALID 未 acquire；BM_ERR_NOT_SUPPORTED BLOCK/LATEST(Task 5 前占位)
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
    if (st->mode == BM_BUS_LATEST) {
        /* 占位：Task 5 Step 5.4 替换为 latest_published 提交分支 */
        return BM_ERR_NOT_SUPPORTED;
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
        /* LATEST：不持游标，不占配额 */
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
        /* LATEST：acquire 序读 latest_published，登记 reading 标记防撕裂 */
        uint32_t pub = bus_load_cur(&st->latest_published);
        bus_store_cur(&st->latest_reading, pub);
        /* 重读 published 确认期间未被写者覆盖（与 bm_snapshot 一致性循环同构）；
         * 若已变，采用新值并更新 reading 标记 */
        {
            uint32_t pub2 = bus_load_cur(&st->latest_published);
            if (pub2 != pub) {
                pub = pub2;
                bus_store_cur(&st->latest_reading, pub);
            }
        }
        *slot_out = st->data_buf + pub * st->elem_size;
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
 * @brief 归还读槽；QUEUE/SIGNAL 推进读者游标，LATEST 清 reading 标记
 *
 * LATEST：将 latest_reading 清为 BM_BUS_LATEST_NONE，使写者 choose 可重新
 *   使用该槽（不再是纯空操作）。
 *
 * @param r 读者句柄
 * @return BM_OK 成功；BM_ERR_INVALID 参数错
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
        uint32_t rc = bus_load_cur(&st->readers[r->slot_idx].read_cur);
        bus_store_cur(&st->readers[r->slot_idx].read_cur, rc + 1u);
    }
    BUS_UNLOCK(s);
    return BM_OK;
}

/**
 * @brief 查询读者可读槽数（Task 4 补全，此处占位）
 *
 * @param r 读者句柄指针
 * @return 0（Task 4 补全）
 */
uint32_t bm_bus_ready_count(const bm_bus_reader_t *r) {
    (void)r;
    return 0u;
}

/**
 * @brief 获取 bus 统计快照
 *
 * @param h   bus 句柄指针
 * @param out 输出统计结构体指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_bus_stats(const bm_bus_t *h, bm_bus_stats_t *out) {
    if (!h || !h->storage || !out) {
        return BM_ERR_INVALID;
    }
    out->write_count    = bus_load_cur(&h->storage->write_cur);
    out->overflow_count = 0u;
    return BM_OK;
}

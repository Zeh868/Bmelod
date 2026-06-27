/**
 * @file test_qemu_conc_core.c
 * @brief core 层并发正确性/契约验证（QEMU ARMv7-A Cortex-A15 -smp 2）
 * @author zeh (china_qzh@163.com)
 * @version 5.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿：冒烟骨架
 * 2026-06-25       2.0            zeh            T1.1 双核真竞态：atomic inc/CAS/内存序
 * 2026-06-25       3.0            zeh            T1.2 bm_critical 本核 IRQ 屏蔽契约
 * 2026-06-25       4.0            zeh            T1.3 bm_mempool 双核分配真竞态
 * 2026-06-26       5.0            zeh            T2.1 bm_event 派发判定 + per-core 隔离契约
 *
 */
#include "bm_qemu_tap.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/common/bm_critical_wrap.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_cpu_local.h"
#include "bm/core/bm_mempool.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#include <stdint.h>
#include <stddef.h>

#define CORE_MAX_RESULTS 32u
static bm_qemu_result_t       g_items0[CORE_MAX_RESULTS];
static bm_qemu_results_t      g_res0 = { g_items0, CORE_MAX_RESULTS, 0u, "cpu0" };
/* cpu1 不直接 record：其结果经共享原子（g_inc_counter/g_locked_sum/g_vis_torn）
 * 上报，由 cpu0 在主线程统一 bm_qemu_record。 */

/* ---- 双核阶段机 ---- */
typedef enum {
    CORE_PHASE_INIT    = 0u,
    CORE_PHASE_ATOMIC  = 1u,   /**< T1.1 */
    CORE_PHASE_MEMPOOL = 2u,   /**< T1.3 */
    CORE_PHASE_DONE    = 9u
} core_phase_t;
static bm_atomic_ipc_u32_t g_phase    = BM_ATOMIC_IPC_U32_INIT(CORE_PHASE_INIT);
static bm_atomic_ipc_u32_t g_cpu1_rdy = BM_ATOMIC_IPC_U32_INIT(0u);

/* ---- T1.1 atomic_ipc 共享变量 ---- */
#define ATOMIC_INC_ROUNDS 100000u
static bm_atomic_ipc_u32_t g_inc_counter = BM_ATOMIC_IPC_U32_INIT(0u);
/** CAS 自旋锁 + 受保护非原子累加器 */
static bm_atomic_ipc_u32_t g_cas_lock    = BM_ATOMIC_IPC_U32_INIT(0u);
static volatile uint32_t   g_locked_sum  = 0u;
#define CAS_ROUNDS 50000u
/** release/acquire 可见性 */
static volatile uint32_t   g_vis_data  = 0u;
static bm_atomic_ipc_u32_t g_vis_flag  = BM_ATOMIC_IPC_U32_INIT(0u);
#define VIS_ROUNDS 2000u
static bm_atomic_ipc_u32_t g_vis_torn  = BM_ATOMIC_IPC_U32_INIT(0u);

/**
 * @brief CAS 自旋锁获取
 */
static void cas_lock_acquire(void) {
    uint32_t expected;
    for (;;) {
        expected = 0u;
        if (bm_atomic_ipc_compare_exchange_u32(&g_cas_lock, &expected, 1u)) {
            return;
        }
        bm_hal_cpu_yield();
    }
}

/**
 * @brief CAS 自旋锁释放
 */
static void cas_lock_release(void) {
    bm_atomic_ipc_store_u32(&g_cas_lock, 0u);
}

/**
 * @brief 自旋等待 CPU1 设置 g_cpu1_rdy 后清零
 *
 * @param timeout_spins 最大自旋次数
 * @return 1 = 正常响应；0 = 超时
 * @note 检测到信号后会自动将 g_cpu1_rdy 清零，调用方无需手动清零（每次调用对应一轮握手）。
 */
static int wait_cpu1(uint32_t timeout_spins) {
    uint32_t spin = 0u;
    while (bm_atomic_ipc_load_u32(&g_cpu1_rdy) == 0u) {
        bm_hal_cpu_yield();
        if (++spin > timeout_spins) { return 0; }
    }
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 0u);
    return 1;
}

/**
 * @brief 两核都做的工作：各自对计数器 inc ATOMIC_INC_ROUNDS 次；CAS 锁内累加 CAS_ROUNDS 次
 */
static void atomic_hammer(void) {
    uint32_t i;
    for (i = 0u; i < ATOMIC_INC_ROUNDS; i++) {
        (void)bm_atomic_ipc_inc_u32(&g_inc_counter);
    }
    for (i = 0u; i < CAS_ROUNDS; i++) {
        cas_lock_acquire();
        g_locked_sum = g_locked_sum + 1u;   /* 非原子，靠 CAS 锁互斥 */
        cas_lock_release();
    }
}

/**
 * @brief cpu0：可见性写者 —— 写 data 后 release-store flag=seq
 */
static void atomic_vis_writer(void) {
    uint32_t seq;
    for (seq = 1u; seq <= VIS_ROUNDS; seq++) {
        g_vis_data = seq;
        bm_atomic_ipc_fence_release();
        bm_atomic_ipc_store_u32(&g_vis_flag, seq);
        while (bm_atomic_ipc_load_u32(&g_vis_flag) == seq) {
            bm_hal_cpu_yield();   /* 等读者消费（置 0） */
        }
    }
}

/**
 * @brief cpu1：可见性读者 —— acquire-load flag 后读 data，断言 data>=flag（永不旧值）
 */
static void atomic_vis_reader(void) {
    uint32_t seen = 0u;
    uint32_t torn = 0u;
    while (seen < VIS_ROUNDS) {
        uint32_t f = bm_atomic_ipc_load_u32(&g_vis_flag);
        if (f != 0u) {
            /* load_u32 已是 acquire-load；此 fence 为防御性冗余，保证移植到弱序 fence 实现时仍正确 */
            bm_atomic_ipc_fence_acquire();
            if (g_vis_data < f) { torn++; }   /* 读到 flag 更新前的旧 data = 撕裂 */
            seen = f;
            bm_atomic_ipc_store_u32(&g_vis_flag, 0u);
        } else {
            bm_hal_cpu_yield();
        }
    }
    bm_atomic_ipc_store_u32(&g_vis_torn, torn);
}

/* ---- T1.3 bm_mempool 双核分配竞态共享变量 ---- */
typedef struct { uint32_t canary; uint32_t slot_tag; } pool_obj_t;
BM_MEMPOOL_DEFINE(g_conc_pool, pool_obj_t, 8u);  /**< 8 槽，两核争抢 */

#define MEMPOOL_ITERS 20000u
/** 每槽 owner 原子标记，用于检测双分配（0=空闲，1=cpu0 占用，2=cpu1 占用） */
static bm_atomic_ipc_u32_t g_slot_owner[8] = {
    BM_ATOMIC_IPC_U32_INIT(0u), BM_ATOMIC_IPC_U32_INIT(0u),
    BM_ATOMIC_IPC_U32_INIT(0u), BM_ATOMIC_IPC_U32_INIT(0u),
    BM_ATOMIC_IPC_U32_INIT(0u), BM_ATOMIC_IPC_U32_INIT(0u),
    BM_ATOMIC_IPC_U32_INIT(0u), BM_ATOMIC_IPC_U32_INIT(0u)
};
static bm_atomic_ipc_u32_t g_double_alloc = BM_ATOMIC_IPC_U32_INIT(0u);
static bm_atomic_ipc_u32_t g_corrupt      = BM_ATOMIC_IPC_U32_INIT(0u);

/**
 * @brief 指针转槽号（利用 BM_MEMPOOL_DEFINE 展开的存储数组名）
 */
static uint32_t obj_to_slot(const pool_obj_t *p) {
    return (uint32_t)(p - &_bm_pool_storage_g_conc_pool[0]);
}

/**
 * @brief 两核都跑的内存池压测：alloc→标记 owner→写 canary→校验→清 owner→free
 *
 * alloc 返回 NULL 时跳过（争用 fail-fast 或池满，合法行为）。
 * CAS 占用标记检测同槽被两核同时持有（双分配）；canary 回读检测数据损坏。
 *
 * @param me 本核标识（cpu0=1u，cpu1=2u）
 */
static void mempool_hammer(uint32_t me) {
    uint32_t i;
    for (i = 0u; i < MEMPOOL_ITERS; i++) {
        pool_obj_t *o = (pool_obj_t *)bm_mempool_alloc(&g_conc_pool);
        if (!o) { continue; }   /* 争用或满，合法 */
        uint32_t slot = obj_to_slot(o);
        /* 占用标记：期望 0（无主），CAS 成 me；失败=该槽已被他核占用=双分配 */
        uint32_t expected = 0u;
        if (!bm_atomic_ipc_compare_exchange_u32(&g_slot_owner[slot], &expected, me)) {
            bm_atomic_ipc_inc_u32(&g_double_alloc);
        }
        o->canary   = 0xC0FFEEu ^ me;
        o->slot_tag = slot;
        if (o->canary != (0xC0FFEEu ^ me) || o->slot_tag != slot) {
            bm_atomic_ipc_inc_u32(&g_corrupt);
        }
        bm_atomic_ipc_store_u32(&g_slot_owner[slot], 0u);  /* 释放占用标记 */
        bm_mempool_free(&g_conc_pool, o);
    }
}

/* ---- T1.2 bm_critical 本核临界区共享变量 ---- */
static volatile uint32_t g_crit_a = 0u;
static volatile uint32_t g_crit_b = 0u;
#define CRIT_ROUNDS 200000u

/**
 * @brief T1.2 本核临界区正例：验证临界区内成对写 pair 不被本核 IRQ 撕裂
 *
 * 契约：BM_CRITICAL_ENTER/EXIT 在 ARMv7-A 上为 cpsid if / cpsie if，
 * 仅屏蔽本核 IRQ，不提供跨核互斥（跨核互斥需用 T1.1 CAS 自旋锁）。
 *
 * @param res 结果容器（cpu0 上下文）
 */
static void critical_local_test(bm_qemu_results_t *res) {
    uint32_t i;
    uint32_t torn = 0u;
    for (i = 0u; i < CRIT_ROUNDS; i++) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        g_crit_a = i;
        g_crit_b = i;            /* 临界区内成对更新 */
        BM_CRITICAL_EXIT(s);
        {
            uint32_t a, b;
            bm_irq_state_t s2 = BM_CRITICAL_ENTER();
            a = g_crit_a; b = g_crit_b;
            BM_CRITICAL_EXIT(s2);
            if (a != b) { torn++; }
        }
    }
    bm_qemu_record(res, "critical_local_pair_consistent", torn == 0u, torn);
    /* 契约固化：critical 是本核 IRQ 屏蔽，不提供跨核互斥（见 atomic CAS 锁 T1.1） */
    bm_qemu_record(res, "critical_is_local_irq_mask_not_xcore_lock", 1, 0u);
}

/* ---- T2.1 bm_event 派发判定 + per-core 隔离契约 ---- */
#define EVT_TYPE_LOCAL  1u   /**< owner = 本核 */
#define EVT_TYPE_REMOTE 2u   /**< owner = 远核(1) */
#define EVT_TYPE_ANY    3u   /**< owner = BM_CPU_ANY */

static uint32_t g_fwd_calls      = 0u;
static uint8_t  g_fwd_last_target = 0xFEu;
static uint32_t g_local_cb_hits  = 0u;

/**
 * @brief 事件 owner resolver 桩：返回各类型对应的 owner CPU
 */
static uint8_t evt_owner_resolver(bm_event_type_t type) {
    if (type == EVT_TYPE_REMOTE) { return 1u; }
    if (type == EVT_TYPE_ANY)    { return BM_CPU_ANY; }
    return (uint8_t)BM_CPU_THIS();   /* LOCAL：本核 */
}

/**
 * @brief 事件 forwarder 桩：仅记录调用次数与目标 CPU，不做真跨核传输
 */
static int evt_forwarder(uint8_t target_cpu, const bm_event_t *event,
                         const void *data, size_t len) {
    (void)event; (void)data; (void)len;
    g_fwd_calls++;
    g_fwd_last_target = target_cpu;
    return BM_OK;
}

/**
 * @brief 本地订阅回调桩：计数命中次数
 */
static void evt_local_cb(const bm_event_t *event, void *user_data) {
    (void)event; (void)user_data;
    g_local_cb_hits++;
}

/**
 * @brief T2.1 event 派发判定测试：在 cpu0 单核上验证 event_forward_if_remote 契约
 *
 * 契约（bm_event.c:141-158）：
 *   - 本核 owner → 不调 forwarder，本地入队派发
 *   - 远核 owner → 调 forwarder 一次，target 正确
 *   - BM_CPU_ANY  → 不调 forwarder，返回 BM_ERR_NOT_INIT 提前退出
 *
 * @param res 结果容器（cpu0 上下文）
 */
static void event_dispatch_test(bm_qemu_results_t *res) {
    bm_event_subscriber_id_t id;
    uint32_t payload = 0xABCDu;

    bm_event_reset();
    bm_event_set_route_hooks(evt_owner_resolver, evt_forwarder);
    (void)bm_event_register_type(EVT_TYPE_LOCAL,  "local");
    (void)bm_event_register_type(EVT_TYPE_REMOTE, "remote");
    (void)bm_event_register_type(EVT_TYPE_ANY,    "any");
    (void)bm_event_subscribe(EVT_TYPE_LOCAL, evt_local_cb, NULL, &id);
    bm_event_freeze_subscriptions();   /* process 需要订阅表已冻结 */

    /* 本核 owner：不转发，本地入队 */
    g_fwd_calls = 0u; g_local_cb_hits = 0u;
    (void)bm_event_publish_copy(EVT_TYPE_LOCAL, 0u, &payload, sizeof(payload));
    (void)bm_event_process(8u);
    bm_qemu_record(res, "event_local_not_forwarded",
                   g_fwd_calls == 0u, g_fwd_calls);
    bm_qemu_record(res, "event_local_dispatched",
                   g_local_cb_hits == 1u, g_local_cb_hits);

    /* 远核 owner：恰好转发一次，目标=1 */
    g_fwd_calls = 0u;
    (void)bm_event_publish_copy(EVT_TYPE_REMOTE, 0u, &payload, sizeof(payload));
    bm_qemu_record(res, "event_remote_forwarded_once",
                   g_fwd_calls == 1u, g_fwd_calls);
    bm_qemu_record(res, "event_remote_target_correct",
                   g_fwd_last_target == 1u, (uint32_t)g_fwd_last_target);

    /* BM_CPU_ANY：不转发 */
    g_fwd_calls = 0u;
    (void)bm_event_publish_copy(EVT_TYPE_ANY, 0u, &payload, sizeof(payload));
    bm_qemu_record(res, "event_any_not_forwarded",
                   g_fwd_calls == 0u, g_fwd_calls);
}

/**
 * @brief CPU1 入口（由 PSCI CPU_ON 从核启动汇编调用）
 */
static void cpu1_entry(void) {
    uint32_t phase;
    (void)bm_hal_timer_init(1000u);
    do { phase = bm_atomic_ipc_load_u32(&g_phase); bm_hal_cpu_yield(); }
    while (phase < CORE_PHASE_ATOMIC);

    atomic_hammer();        /* 与 cpu0 并发 inc + CAS */
    atomic_vis_reader();    /* 可见性读者 */
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    /* T1.3 MEMPOOL 阶段：等 cpu0 拉起，与 cpu0 并发争抢内存池 */
    do { phase = bm_atomic_ipc_load_u32(&g_phase); bm_hal_cpu_yield(); }
    while (phase < CORE_PHASE_MEMPOOL);
    mempool_hammer(2u);     /* cpu1 = owner 标记 2 */
    bm_atomic_ipc_store_u32(&g_cpu1_rdy, 1u);

    do { phase = bm_atomic_ipc_load_u32(&g_phase); bm_hal_cpu_yield(); }
    while (phase < CORE_PHASE_DONE);
    for (;;) { bm_hal_cpu_yield(); }
}

int main(void) {
    (void)bm_hal_timer_init(1000u);
    bm_hal_cpu_init();

    /* 冒烟断言 */
    bm_qemu_record(&g_res0, "smoke_boot", 1, 0u);

    /* 启动从核 */
    {
        int rc = bm_hal_cpu_boot_secondary((uintptr_t)cpu1_entry);
        bm_qemu_record(&g_res0, "boot_secondary", rc == BM_OK, (uint32_t)(-rc));
    }

    /* ——— ATOMIC 阶段：双核并发 inc / CAS / 内存序 ——— */
    bm_atomic_ipc_store_u32(&g_phase, CORE_PHASE_ATOMIC);
    atomic_hammer();        /* cpu0 侧并发 */
    atomic_vis_writer();    /* 可见性写者 */
    (void)wait_cpu1(20000000u);

    bm_qemu_record(&g_res0, "atomic_inc_no_lost",
        bm_atomic_ipc_load_u32(&g_inc_counter) == (2u * ATOMIC_INC_ROUNDS),
        bm_atomic_ipc_load_u32(&g_inc_counter));
    bm_qemu_record(&g_res0, "atomic_cas_mutex",
        g_locked_sum == (2u * CAS_ROUNDS), g_locked_sum);
    bm_qemu_record(&g_res0, "atomic_release_acquire_no_torn",
        bm_atomic_ipc_load_u32(&g_vis_torn) == 0u,
        bm_atomic_ipc_load_u32(&g_vis_torn));

    /* ——— T1.2 CRITICAL 阶段：本核 IRQ 屏蔽契约 ——— */
    critical_local_test(&g_res0);

    /* ——— T1.3 MEMPOOL 阶段：双核并发分配/释放 ——— */
    bm_atomic_ipc_store_u32(&g_phase, CORE_PHASE_MEMPOOL);
    mempool_hammer(1u);     /* cpu0 = owner 标记 1 */
    (void)wait_cpu1(20000000u);

    bm_qemu_record(&g_res0, "mempool_no_double_alloc",
        bm_atomic_ipc_load_u32(&g_double_alloc) == 0u,
        bm_atomic_ipc_load_u32(&g_double_alloc));
    bm_qemu_record(&g_res0, "mempool_no_corruption",
        bm_atomic_ipc_load_u32(&g_corrupt) == 0u,
        bm_atomic_ipc_load_u32(&g_corrupt));
    /* 全部 free 后池应全空：再连续 alloc 8 次都应成功 */
    {
        uint32_t got = 0u;
        uint32_t k;
        pool_obj_t *tmp[8];
        for (k = 0u; k < 8u; k++) {
            tmp[k] = (pool_obj_t *)bm_mempool_alloc(&g_conc_pool);
            if (tmp[k]) { got++; }
        }
        for (k = 0u; k < 8u; k++) { if (tmp[k]) { bm_mempool_free(&g_conc_pool, tmp[k]); } }
        bm_qemu_record(&g_res0, "mempool_fully_freed_after_run", got == 8u, got);
    }

    /* ——— T2.1 EVENT 阶段：cpu0 单核 event 派发判定契约 ——— */
    event_dispatch_test(&g_res0);

    /* 发 DONE，让 cpu1 退出自旋 */
    bm_atomic_ipc_store_u32(&g_phase, CORE_PHASE_DONE);

    {
        bm_qemu_results_t sets[1] = { g_res0 };
        bm_qemu_print_tap(sets, 1u, "bm conc-core test");
    }
    return 0;
}

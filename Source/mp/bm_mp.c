/**
 * @file bm_mp.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief BM_MP_PERCPU 剖面入口与主循环
 *
 * 主循环顺序：stream drain → IPC drain → ticker → event process。
 * @author zeh (china_qzh@163.com)
 * @version 1.7
 * @date 2026-06-29
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            hard RT deadline miss safe-stop
 * 2026-06-14       1.2            zeh            per-CPU log ring drain
 * 2026-06-15       1.3            zeh            主循环周期等待有界并冻结 iter hook
 * 2026-06-15       1.4            zeh            周期等待超限饱和计数器诊断
 * 2026-06-15       1.5            zeh            从核入口补定时器初始化以支撑 boot 超时
 * 2026-06-18       1.6            zeh            周期判定抽为可测纯函数；超限计数原子化并可查询；
 *                                               硬实时下周期超限/IPC 序列异常触发 safe-stop
 * 2026-06-29       1.7            zeh            mp_period_overrun_record CAS 环加 F-6 重试上界
 *
 */
#include "bm/mp/bm_mp.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_ipc.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm/hybrid/bm_stream_relay.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#if BM_CONFIG_ENABLE_TICKER
#include "bm_ticker.h"
#endif

#if BM_CONFIG_ENABLE_HRT
#include "bm_hrt.h"
#endif

#if BM_CONFIG_ENABLE_MODULE
#include "bm_module.h"
#endif

#if BM_CONFIG_ENABLE_EXEC
#include "bm_exec.h"
#endif

#include "bm/common/bm_safety.h"

#include <stddef.h>

/* Forward declaration of WDG gate installer */
void bm_mp_install_wdg_gate(void);

#ifndef BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS
#define BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS  100000u
#endif

/**
 * 诊断：主循环周期等待超限次数（饱和计数）。
 * 各核共享，故用原子 CAS 饱和递增，避免非原子 RMW 丢失计数。
 */
static bm_atomic_ipc_u32_t s_enforce_period_overrun_count;

uint32_t bm_mp_main_loop_overrun_count(void) {
    return bm_atomic_ipc_load_u32(&s_enforce_period_overrun_count);
}

/**
 * @brief 原子饱和递增周期超限计数（多核共享）
 *
 * [F-6] CAS 环上界与 bm_critical.c:bm_atomic_inc 对齐：
 * 争用超 BM_CONFIG_ATOMIC_MAX_RETRIES 次后饱和写入 UINT32_MAX，
 * 避免无限自旋拖死主循环（诊断计数容忍饱和语义，无需精确值）。
 * 注意：s_enforce_period_overrun_count 类型为 bm_atomic_ipc_u32_t（跨核原子），
 * 不可替换为非 ipc 原子操作。
 */
static void mp_period_overrun_record(void) {
    uint32_t retry;
    uint32_t cur = bm_atomic_ipc_load_u32(&s_enforce_period_overrun_count);

    for (retry = 0u; retry < BM_CONFIG_ATOMIC_MAX_RETRIES; retry++) {
        uint32_t desired;

        if (cur == UINT32_MAX) {
            return;
        }
        desired = cur + 1u;
        if (bm_atomic_ipc_compare_exchange_u32(
                &s_enforce_period_overrun_count, &cur, desired)) {
            return;
        }
        /* CAS 失败时 cur 已被更新为当前实际值，直接重试 */
    }
    /* [F-6] 争用超 BM_CONFIG_ATOMIC_MAX_RETRIES 次，饱和到 UINT32_MAX */
    bm_atomic_ipc_store_u32(&s_enforce_period_overrun_count, UINT32_MAX);
}

int bm_mp_main_loop_period_elapsed(uint32_t start_ticks, uint32_t now_ticks,
                                   uint32_t freq, uint32_t period_us) {
    uint32_t elapsed_ticks;
    uint64_t elapsed_us;

    if (period_us == 0u || freq == 0u) {
        return 1;
    }
    /* 回绕安全：now - start 在 uint32_t 模算下对单周期始终正确。 */
    elapsed_ticks = now_ticks - start_ticks;
    elapsed_us = ((uint64_t)elapsed_ticks * 1000000ull) / (uint64_t)freq;
    return (elapsed_us >= (uint64_t)period_us) ? 1 : 0;
}

static bm_atomic_ipc_u32_t s_demo_max_loops =
    BM_ATOMIC_IPC_U32_INIT(2000u);
/*
 * 确定性流式跨核可见性：demo stop 标志使用原子操作，
 * 保证在无缓存一致性硬件的 AMP 上也具有跨核可见性。
 * volatile 不足以保证 ARM 弱内存序下的跨核同步。
 */
static bm_atomic_ipc_u32_t s_demo_stop;
static bm_atomic_ipc_u32_t s_cpu_iter_hook_frozen;
static bm_mp_cpu_init_fn_t s_cpu_init_hook[BM_CONFIG_CPU_COUNT];
static bm_mp_cpu_iter_fn_t s_cpu_iter_hook[BM_CONFIG_CPU_COUNT];

void bm_mp_set_demo_max_loops(uint32_t max_loops) {
    bm_atomic_ipc_store_u32(&s_demo_max_loops, max_loops);
}

void bm_mp_signal_demo_stop(void) {
    bm_atomic_ipc_store_u32(&s_demo_stop, 1u);
}

int bm_mp_join_secondary_cpus(void) {
    return bm_hal_cpu_join_secondary();
}

void bm_mp_set_cpu_init_hook(uint8_t cpu, bm_mp_cpu_init_fn_t fn) {
    if (cpu < BM_CONFIG_CPU_COUNT) {
        s_cpu_init_hook[cpu] = fn;
    }
}

void bm_mp_set_cpu_iter_hook(uint8_t cpu, bm_mp_cpu_iter_fn_t fn) {
    if (cpu < BM_CONFIG_CPU_COUNT &&
        bm_atomic_ipc_load_u32(&s_cpu_iter_hook_frozen) == 0u) {
        s_cpu_iter_hook[cpu] = fn;
    }
}

int bm_mp_init(uint32_t cpu_count) {
    if (cpu_count != BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    bm_atomic_ipc_store_u32(&s_demo_stop, 0u);
    bm_atomic_ipc_store_u32(&s_cpu_iter_hook_frozen, 0u);
    bm_atomic_ipc_store_u32(&s_enforce_period_overrun_count, 0u);
    bm_stream_relay_registry_reset();
    bm_module_set_freeze_hook(bm_stream_relay_freeze_registry);
    bm_mp_install_wdg_gate();
    bm_hal_cpu_init();
    bm_mp_profile_reset();
    bm_mp_partition_reset();
    bm_mp_resource_topology_reset();
    bm_mp_schedule_reset();
    bm_mp_wdg_reset();
    bm_event_set_route_hooks(
        bm_mp_event_owner, bm_mp_ipc_publish_event_forward);
#if BM_CONFIG_ENABLE_HRT
    bm_hrt_set_start_gate(bm_mp_boot_require_irq_released);
#endif
#if BM_CONFIG_ENABLE_EXEC
    bm_exec_set_irq_release_gate(bm_mp_boot_require_irq_released);
#if defined(BM_CONFIG_MP_HARD_RT_PROFILE) && BM_CONFIG_MP_HARD_RT_PROFILE
    bm_exec_set_deadline_miss_handler(bm_mp_exec_deadline_safe_stop);
    /*
     * 跨核 IPC 序列异常意味着事件流已不可信（跨代陈旧 / 未完成写入）。
     * 硬实时剖面下注册安全停机钩子，使 drain 检测到序列异常时确定性停机，
     * 而非让该 source→target 环静默卡死、事件永久丢失。
     */
    bm_mp_ipc_set_fault_hook(bm_mp_ipc_fault_safe_stop);
#endif
#endif
    return bm_mp_boot_format();
}

#if defined(BM_CONFIG_MP_HARD_RT_PROFILE) && BM_CONFIG_MP_HARD_RT_PROFILE
void bm_mp_exec_deadline_safe_stop(const bm_exec_slot_t *slot,
                                   bm_block_t *block,
                                   uint32_t elapsed_us) {
    (void)slot;
    (void)block;
    (void)elapsed_us;
    bm_exec_safe_stop_all(NULL, 0u);
}

void bm_mp_ipc_fault_safe_stop(uint8_t source_cpu, uint8_t target_cpu) {
    (void)source_cpu;
    (void)target_cpu;
    BM_LOGE("mp", "ipc sequence fault src=%u dst=%u -> safe stop",
            (unsigned)source_cpu, (unsigned)target_cpu);
    bm_exec_safe_stop_all(NULL, 0u);
}
#endif

void bm_mp_idle_until_interrupt(void) {
    bm_hal_cpu_yield();
}

/**
 * @brief 强制主循环最小周期间隔
 *
 * 若 BM_CONFIG_MP_MAIN_LOOP_PERIOD_US > 0，则自 @p iteration_start_ticks 起
 * 忙等待直到至少该周期已过。这是确定性流式的核心约束：确保每核的
 * stream/relay/IPC drain 以可预测的速率执行，从而使 WCET 分析有效。
 *
 * @param iteration_start_ticks 本轮迭代开始时的 bm_hal_timer_get_ticks() 快照
 */
void bm_mp_enforce_main_loop_period(uint32_t iteration_start_ticks) {
    uint32_t period_us = BM_CONFIG_MP_MAIN_LOOP_PERIOD_US;

    if (period_us == 0u) {
        return;
    }
#ifdef NATIVE_SIM
    /*
     * native_sim 定时器仅在显式 bm_hal_timer_native_advance_ticks() 调用时递增。
     * 周期强制在 native_sim 上跳过；仿真主循环通过显式 tick 推进控制时序。
     */
    (void)iteration_start_ticks;
    return;
#else
    {
        uint32_t freq = bm_hal_timer_get_freq();
        if (freq == 0u) {
            return;
        }
        /*
         * [F-4 自旋优化] 消除每轮 u64 除法，改用乘法比较——语义与 us 域判据严格等价：
         *   elapsed_us >= period_us
         *   ⟺  (elapsed_ticks * 1000000) / freq >= period_us
         *   ⟺  elapsed_ticks * 1000000 >= period_us * freq
         *   （period_us 为整数，floor 除法下严格等价，无舍入差异）
         * 循环外预计算 threshold_scaled = period_us * freq（一次乘法），
         * 循环内仅做一次 u64 乘法，去掉每轮 u64 除法，显著降低自旋热路径开销。
         * uint32_t 减法保留回绕安全语义（与 bm_mp_main_loop_period_elapsed 一致）。
         * bm_mp_main_loop_period_elapsed 保持不变，供单元测试独立验证语义。
         */
        uint64_t threshold_scaled = (uint64_t)period_us * (uint64_t)freq;
        uint32_t spins;

        for (spins = 0u; spins < BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS; spins++) {
            uint32_t elapsed_ticks =
                (uint32_t)(bm_hal_timer_get_ticks() - iteration_start_ticks);
            if ((uint64_t)elapsed_ticks * 1000000ull >= threshold_scaled) {
                break;
            }
            bm_hal_cpu_yield();
        }
        if (spins >= BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS) {
            /*
             * 周期未能在自旋上限内强制 => 确定性速率约束本轮失效。
             * 记录诊断；硬实时剖面下时序保证已被破坏，必须安全停机，
             * 而非静默继续（否则 WCET 闭包假设不再成立）。
             */
            mp_period_overrun_record();
            BM_LOGE("mp", "main loop period wait exceeded %u spins",
                    (unsigned)BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS);
            bm_mp_boot_report_failure();
            bm_mp_signal_demo_stop();
#if defined(BM_CONFIG_MP_HARD_RT_PROFILE) && BM_CONFIG_MP_HARD_RT_PROFILE && \
    BM_CONFIG_ENABLE_EXEC
            bm_exec_safe_stop_all(NULL, 0u);
#endif
        }
    }
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void bm_mp_coop_pre_iteration(uint32_t cpu) {
    (void)cpu;
}

void bm_mp_cpu_main_iteration(void) {
    uint32_t cpu = BM_CPU_THIS();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        BM_LOGE("mp", "main iteration on invalid cpu %u (max=%u)",
                (unsigned)cpu, (unsigned)BM_CONFIG_CPU_COUNT);
        return;
    }
    bm_mp_coop_pre_iteration(cpu);
    bm_atomic_ipc_store_u32(&s_cpu_iter_hook_frozen, 1u);
    /*
     * 固定 drain 顺序保证 WCET 可静态求和：先本地 stream/relay，
     * 再跨核 IPC 注入事件，最后 ticker 与 event process。
     * 调换顺序会改变事件相对时序，破坏 profile 闭包校验假设。
     */
    (void)bm_stream_drain_on_this_cpu(BM_CONFIG_MP_STREAM_DRAIN_BUDGET);
    (void)bm_stream_relay_drain_on_this_cpu(BM_CONFIG_MP_RELAY_DRAIN_BUDGET);
    (void)bm_ipc_drain_on_this_cpu(BM_CONFIG_MP_IPC_DRAIN_BUDGET);
#if BM_CONFIG_ENABLE_TICKER
    (void)bm_ticker_poll();
#endif
    (void)bm_event_process(BM_CONFIG_MP_EVENT_PROCESS_BUDGET);
#if BM_CONFIG_ENABLE_LOG && BM_CONFIG_LOG_MP_RING
    /*
     * 日志环为 SPSC：各核生产、仅 bootstrap 消费。
     * 放在 event process 之后，避免 drain 日志时重入 event 回调。
     */
    if (bm_hal_cpu_is_bootstrap()) {
        uint32_t c;

        for (c = 0u; c < BM_CONFIG_CPU_COUNT; c++) {
            (void)bm_log_drain_cpu(c, BM_CONFIG_MP_LOG_DRAIN_BUDGET);
        }
    }
#endif
    if (s_cpu_iter_hook[cpu]) {
        s_cpu_iter_hook[cpu]();
    }
    bm_mp_wdg_feed_this_cpu();
}

void bm_mp_cpu_main_limited(uint32_t max_loops) {
    uint32_t loops = 0u;

    for (;;) {
        uint32_t start_ticks = bm_hal_timer_get_ticks();

        bm_mp_cpu_main_iteration();
#if !defined(BM_EXAMPLE_QEMU_SMP)
        bm_mp_enforce_main_loop_period(start_ticks);
#endif
#if BM_CONFIG_MP_MAIN_LOOP_PERIOD_US == 0u
        bm_mp_idle_until_interrupt();
#endif
        if (bm_atomic_ipc_load_u32(&s_demo_stop) != 0u) {
            break;
        }
        if (max_loops > 0u && ++loops >= max_loops) {
            break;
        }
    }
}

void bm_mp_cpu_main(void) {
    bm_mp_cpu_main_limited(0u);
}

void bm_mp_cpu_secondary_entry(void) {
    int rc;
    uint32_t cpu = BM_CPU_THIS();

#if BM_MP_MULTICORE
    /*
     * 从核须在首个带超时的 boot/barrier 之前初始化定时器；
     * 否则 boot_now_us() 恒为 0，屏障与主核无法对齐。
     */
    if (bm_hal_timer_get_freq() == 0u) {
        uint32_t hz = 1000000u / BM_CONFIG_HRT_TICK_US;

        if (hz == 0u) {
            hz = 1000u;
        }
        (void)bm_hal_timer_init(hz);
    }
#endif

    rc = bm_mp_boot_wait_matrix_phase(BM_MP_BOOT_PARTITION_READY,
                                      BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        goto fail;
    }
#if BM_CONFIG_MP_HARD_RT_PROFILE
    rc = bm_mp_boot_wait_matrix_phase(BM_MP_BOOT_PROFILE_READY,
                                      BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        goto fail;
    }
    bm_mp_profile_bind_epoch_on_this_cpu();
#endif
    rc = bm_mp_boot_cpu_attach_and_init();
    if (rc != BM_OK) {
        goto fail;
    }
    rc = bm_mp_barrier_wait(
        BM_MP_BOOT_RUNTIME_READY, BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        goto fail;
    }
    if (cpu < BM_CONFIG_CPU_COUNT && s_cpu_init_hook[cpu]) {
        rc = s_cpu_init_hook[cpu]();
        if (rc != BM_OK) {
            goto fail;
        }
    }
    rc = bm_mp_barrier_wait(
        BM_MP_BOOT_INIT_READY, BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        goto fail;
    }
    rc = bm_mp_barrier_wait(
        BM_MP_BOOT_START_READY, BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        goto fail;
    }
    rc = bm_mp_barrier_wait(
        BM_MP_BOOT_IRQ_RELEASE, BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        goto fail;
    }
#if BM_CONFIG_ENABLE_EXEC
    rc = bm_exec_irq_release_on_this_cpu();
    if (rc != BM_OK) {
        goto fail;
    }
#endif
    bm_mp_cpu_main_limited(
        bm_atomic_ipc_load_u32(&s_demo_max_loops));
    return;

fail:
    bm_mp_boot_report_failure();
}

int bm_event_register_type_owner(bm_event_type_t type,
                                    const char *name,
                                    uint8_t owner_cpu) {
    return bm_mp_partition_register_event_owner(type, name, owner_cpu);
}

int bm_stream_drain_on_this_cpu(uint32_t budget) {
#if BM_CONFIG_ENABLE_EXEC
    int drained = bm_exec_drain_streams(budget);
#if BM_MP_MULTICORE
    if (drained > 0) {
        uint32_t cpu = BM_CPU_THIS();
        int i;

        for (i = 0; i < drained; i++) {
            bm_mp_ipc_count_stream_block((uint8_t)cpu);
        }
    }
#endif
    return drained;
#else
    (void)budget;
    return 0;
#endif
}

/**
 * @file bm_mp_boot.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief PERCPU 多阶段启动状态机实现
 *
 * native_sim 单核路径简化为本地状态机；多核时通过共享矩阵 boot_phase 同步。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "bm/mp/bm_mp_boot.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_ipc.h"
#include "bm/mp/bm_mp_cpu.h"
#include "bm_event.h"
#include "bm_log.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"

#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif

extern void bm_mp_ipc_matrix_use_static_storage(void);

typedef struct {
    bm_mp_boot_phase_t phase;   /**< 本核当前启动阶段 */
    uint32_t boot_epoch;        /**< 与共享矩阵同步的启动代际 */
    int irq_released;           /**< IRQ 是否已放行 */
} bm_mp_boot_cpu_state_t;

/** per-CPU 缓存行对齐存储，避免伪共享 */
typedef struct {
    bm_mp_boot_cpu_state_t state;
    uint8_t padding[(sizeof(bm_mp_boot_cpu_state_t) % BM_CONFIG_CACHE_LINE)
        ? (BM_CONFIG_CACHE_LINE - (sizeof(bm_mp_boot_cpu_state_t) %
                                   BM_CONFIG_CACHE_LINE))
        : 0];
} bm_mp_boot_cpu_storage_t;

/** 各逻辑 CPU 的本地启动状态（cache-line 隔离） */
static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_mp_boot_cpu_storage_t s_boot_cpu[BM_CONFIG_CPU_COUNT];

/** 返回当前逻辑 CPU 的本地启动状态；越界返回 NULL */
static bm_mp_boot_cpu_state_t *boot_this_cpu(void) {
    uint32_t cpu = BM_CPU_THIS();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &s_boot_cpu[cpu].state;
}

/** 将本核与共享矩阵的 boot_phase 同步发布为 FAILED，供从核 wait 路径感知 */
static void boot_publish_failed(void) {
    bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
    bm_mp_boot_cpu_state_t *local = boot_this_cpu();

    if (local) {
        local->phase = BM_MP_BOOT_FAILED;
    }
    if (matrix) {
        bm_atomic_ipc_store_u32(
            &matrix->boot_phase, (uint32_t)BM_MP_BOOT_FAILED);
    }
}

void bm_mp_boot_report_failure(void) {
    boot_publish_failed();
}

int bm_mp_boot_format(void) {
    memset(s_boot_cpu, 0, sizeof(s_boot_cpu));
    bm_mp_ipc_matrix_use_static_storage();
    return BM_OK;
}

int bm_mp_boot_bootstrap_sequence(void) {
    int rc;

    if (!bm_hal_cpu_is_bootstrap()) {
        return BM_ERR_INVALID;
    }

    rc = bm_mp_partition_build_and_validate();
    if (rc != BM_OK) {
        boot_publish_failed();
        return rc;
    }
    {
        bm_mp_boot_cpu_state_t *local = boot_this_cpu();
        if (!local) {
            boot_publish_failed();
            return BM_ERR_INVALID;
        }
        local->phase = BM_MP_BOOT_PARTITION_READY;
    }

    if (BM_MP_MULTICORE) {
        bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
        if (matrix) {
            const bm_mp_partition_t *part = bm_mp_partition();
            bm_mp_boot_cpu_state_t *local = boot_this_cpu();
            if (local) {
                local->boot_epoch =
                    bm_atomic_ipc_inc_u32(&matrix->boot_epoch);
            }
            /*
             * partition_crc 写入 happens-before 下方 boot_phase 的 release-store；
             * 从核 acquire-load boot_phase 后再读 partition_crc，形成有效
             * happens-before 链；partition_crc 本身无需原子限定。
             */
            matrix->partition_crc = part ? part->partition_crc : 0u;
            bm_atomic_ipc_store_u32(
                &matrix->boot_phase, (uint32_t)BM_MP_BOOT_PARTITION_READY);
        }
    }

    BM_LOGI("mp_boot", "bootstrap partition ready");
    return BM_OK;
}

int bm_mp_boot_cpu_attach_and_init(void) {
    const bm_mp_partition_t *part;
    uint32_t cpu = BM_CPU_THIS();

    (void)cpu;
    if (!bm_mp_cpu_valid()) {
        return BM_ERR_INVALID;
    }

#if BM_MP_MULTICORE && BM_CONFIG_MP_HARD_RT_PROFILE
    if (!bm_mp_profile_is_built()) {
        return BM_ERR_NOT_INIT;
    }
#endif

    part = bm_mp_partition();
    if (!part) {
        return BM_ERR_NOT_INIT;
    }

    if (BM_MP_MULTICORE) {
        bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
        uint32_t shared_epoch = matrix ?
            bm_atomic_ipc_load_u32(&matrix->boot_epoch) : 0u;
        if (!matrix || shared_epoch == 0u ||
            matrix->partition_crc != part->partition_crc) {
            /*
             * CRC 不一致说明 bootstrap 正在重建分区或从核读到陈旧矩阵；
             * 拒绝 attach 可避免在错误 owner 映射上注册事件类型。
             */
            BM_LOGE("mp_boot", "cpu %u partition crc mismatch",
                    (unsigned)cpu);
            boot_publish_failed();
            return BM_ERR_INVALID;
        }
        {
            bm_mp_boot_cpu_state_t *local = boot_this_cpu();
            if (local) {
                local->boot_epoch = shared_epoch;
            }
        }
    }

    /*
     * 各核独立 reset 事件子系统：PERCPU 下每核一份队列/订阅表，
     * 必须在 register_events 之前清零，避免残留测试状态。
     */
    bm_event_reset();
    if (bm_mp_partition_register_events_on_this_cpu() != BM_OK) {
        boot_publish_failed();
        return BM_ERR_INVALID;
    }
    {
        bm_mp_boot_cpu_state_t *local = boot_this_cpu();
        if (!local) {
            boot_publish_failed();
            return BM_ERR_INVALID;
        }
        local->phase = BM_MP_BOOT_RUNTIME_READY;
    }
    BM_LOGD("mp_boot", "cpu %u runtime ready", (unsigned)cpu);
    return BM_OK;
}

int bm_mp_boot_signal_profile_ready(void) {
    bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();

    if (!bm_mp_profile_is_built()) {
        return BM_ERR_NOT_INIT;
    }
    {
        bm_mp_boot_cpu_state_t *local = boot_this_cpu();
        if (!local) {
            return BM_ERR_INVALID;
        }
        local->phase = BM_MP_BOOT_PROFILE_READY;
    }
    if (BM_MP_MULTICORE && matrix) {
        bm_atomic_ipc_store_u32(
            &matrix->boot_phase, (uint32_t)BM_MP_BOOT_PROFILE_READY);
    }
    return BM_OK;
}

uint32_t bm_mp_boot_epoch(void) {
    bm_mp_boot_cpu_state_t *local = boot_this_cpu();

    return local ? local->boot_epoch : 0u;
}

/**
 * @brief 无有效 HRT tick 配置时的 fallback tick 周期（微秒）
 *
 * 依据：与 bm_config.h 中 BM_CONFIG_HRT_TICK_US 的默认值（100us）一致，
 * 仅当运行期 BM_CONFIG_HRT_TICK_US 被覆盖为 0 时兜底，避免除零/零周期。
 */
#define MP_BOOT_FALLBACK_TICK_US  100u

static uint64_t boot_now_us(void) {
#if defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;

    if (QueryPerformanceFrequency(&frequency) &&
        QueryPerformanceCounter(&counter) &&
        frequency.QuadPart > 0) {
        return ((uint64_t)counter.QuadPart * 1000000ull) /
               (uint64_t)frequency.QuadPart;
    }
    return (uint64_t)GetTickCount() * 1000ull;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000ull +
               (uint64_t)ts.tv_nsec / 1000ull;
    }
#endif
    {
        uint32_t tick_us = BM_CONFIG_HRT_TICK_US;
        if (tick_us == 0u) {
            tick_us = MP_BOOT_FALLBACK_TICK_US;
        }
        return (uint64_t)bm_hal_timer_get_ticks() * (uint64_t)tick_us;
    }
}

static int boot_timed_out(uint64_t start_us, uint32_t timeout_us) {
    if (timeout_us == 0u) {
        return 0;
    }
    return (boot_now_us() - start_us) >= (uint64_t)timeout_us;
}

#if BM_MP_MULTICORE
static int barrier_all_cpus_at(bm_mp_boot_phase_t phase, uint32_t timeout_us) {
    bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
    uint32_t cpu = BM_CPU_THIS();
    uint64_t start_us = boot_now_us();

    if (!matrix) {
        return BM_ERR_NOT_INIT;
    }

    if (bm_atomic_ipc_load_u32(&matrix->cpu_ready[cpu]) >=
        (uint32_t)phase) {
        /* 重复 barrier 调用：防止同一阶段二次登记导致竞态 */
        return BM_ERR_INVALID;
    }
    bm_atomic_ipc_store_u32(&matrix->cpu_ready[cpu], (uint32_t)phase);

    /*
     * Barrier 正确性：cpu_ready[c] 仅单调递增（阶段不回退）。
     * 在判定 all_ready 与下方写 boot_phase 之间，其他 CPU 只会更就绪、
     * 不会回退，故 barrier 不会被破坏：检查时所有 CPU 已达 phase，
     * 则写 boot_phase 时仍满足。
     */
    for (;;) {
        uint32_t c;
        int all_ready = 1;

        if (bm_atomic_ipc_load_u32(&matrix->boot_phase) ==
            (uint32_t)BM_MP_BOOT_FAILED) {
            return BM_ERR_INVALID;
        }
        for (c = 0u; c < BM_CONFIG_CPU_COUNT; c++) {
            if (bm_atomic_ipc_load_u32(&matrix->cpu_ready[c]) <
                (uint32_t)phase) {
                all_ready = 0;
                break;
            }
        }
        if (all_ready) {
            bm_atomic_ipc_store_u32(&matrix->boot_phase, (uint32_t)phase);
            return BM_OK;
        }
        if (boot_timed_out(start_us, timeout_us)) {
            return BM_ERR_TIMEOUT;
        }
        bm_hal_cpu_yield();
    }
}
#endif

int bm_mp_boot_wait_matrix_phase(bm_mp_boot_phase_t phase, uint32_t timeout_us) {
    bm_mp_ipc_matrix_t *matrix = bm_mp_ipc_matrix();
    uint64_t start_us = boot_now_us();

    if (!matrix) {
        return BM_ERR_NOT_INIT;
    }
    if (timeout_us == 0u) {
        timeout_us = BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US;
    }

    for (;;) {
        uint32_t shared_phase =
            bm_atomic_ipc_load_u32(&matrix->boot_phase);

        if (shared_phase == (uint32_t)BM_MP_BOOT_FAILED) {
            return BM_ERR_INVALID;
        }
        if (shared_phase >= (uint32_t)phase) {
            return BM_OK;
        }
        if (boot_timed_out(start_us, timeout_us)) {
            return BM_ERR_TIMEOUT;
        }
        bm_hal_cpu_yield();
    }
}

int bm_mp_barrier_wait(bm_mp_boot_phase_t phase, uint32_t timeout_us) {
    bm_mp_boot_cpu_state_t *local = boot_this_cpu();

    if (!local || !bm_mp_cpu_valid() || local->phase == BM_MP_BOOT_FAILED ||
        phase <= BM_MP_BOOT_INIT || phase < local->phase ||
        phase >= BM_MP_BOOT_FAILED) {
        return BM_ERR_INVALID;
    }
    if (timeout_us == 0u) {
        timeout_us = BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US;
    }

#if BM_MP_MULTICORE
    int rc = barrier_all_cpus_at(phase, timeout_us);
    if (rc != BM_OK) {
        boot_publish_failed();
        return rc;
    }
#else
    (void)timeout_us;
#endif

    local->phase = phase;
    if (phase == BM_MP_BOOT_IRQ_RELEASE) {
        local->irq_released = 1;
    }
    return BM_OK;
}

int bm_mp_boot_is_irq_released(void) {
    bm_mp_boot_cpu_state_t *local = boot_this_cpu();

    return (local != NULL && local->irq_released != 0) ? 1 : 0;
}

int bm_mp_boot_require_irq_released(void) {
    return bm_mp_boot_is_irq_released() ? BM_OK : BM_ERR_NOT_INIT;
}

void bm_mp_boot_release_irq(void) {
    /*
     * 多核路径不得在此提前写 matrix->boot_phase。
     * IRQ_RELEASE 仅由 bm_mp_barrier_wait(BM_MP_BOOT_IRQ_RELEASE) 在
     * 全核 cpu_ready 对齐后发布，避免从核在 exec/HRT 未 prepare 时误判已放行。
     */
#if !BM_MP_MULTICORE
    {
        bm_mp_boot_cpu_state_t *local = boot_this_cpu();
        if (local) {
            local->phase = BM_MP_BOOT_IRQ_RELEASE;
            local->irq_released = 1;
        }
    }
#endif
}

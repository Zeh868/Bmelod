/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_timer.c
 * @brief 定时器 HAL 分发层（契约 → driver API）
 *
 * 有 BM_DRV_HAS_BACKEND 时转发至 Port driver API；否则提供桩实现。
 * native_sim 路径可经 BM_NATIVE_SIM_TIMER_CPU_LOCAL 启用软件定时器。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            非法 CPU timer handle fail-closed
 * 2026-06-15       1.2            zeh            非法 CPU epoch 使用无效哨兵
 *
 */
#include "bm_drv_timer.h"
#include "bm_hal_timer.h"
#include "bm/hybrid/bm_timestamp.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_types.h"
#include "hal/bm_hal_cache.h"
#include "bm/common/bm_atomic_ipc.h"

#if defined(BM_NATIVE_SIM_TIMER_CPU_LOCAL)
#include "bm_hal_timer_native.h"
#endif

#ifdef BM_DRV_HAS_BACKEND
extern const struct bm_timer_driver_api bm_drv_timer_api;
#define BM_TIMER_DRV (&bm_drv_timer_api)
#else
static int timer_stub_init(uint32_t freq_hz) {
    (void)freq_hz;
    return BM_ERR_NOT_INIT;
}

static void timer_stub_stop(void) {
}

static uint32_t timer_stub_get_ticks(void) {
    return 0u;
}

static uint32_t timer_stub_get_freq(void) {
    return 0u;
}

static void timer_stub_set_callback(void (*cb)(void)) {
    (void)cb;
}

static const struct bm_timer_driver_api timer_stub = {
    timer_stub_init,
    timer_stub_stop,
    timer_stub_get_ticks,
    timer_stub_get_freq,
    timer_stub_set_callback,
};

#define BM_TIMER_DRV (&timer_stub)
#endif

/*
 * CPU 维度的时钟 epoch 数组——按 CPU 路由时须保证可见性：
 * - 缓存一致性硬件：release/acquire 配对即足够。
 * - 非一致缓存平台：本核在非 NOOP D-cache 平台额外执行
 *   cache clean/invalidate；仍强烈建议将 epoch 置于不可缓存 SRAM
 *   （MPU/MMU 属性）以获得确定性 WCET。
 */
static bm_atomic_ipc_u32_t s_clock_epoch[BM_CONFIG_CPU_COUNT];

int bm_hal_timer_init(uint32_t freq_hz) {
    if (!BM_TIMER_DRV->init) {
        return BM_ERR_NOT_INIT;
    }
    return BM_TIMER_DRV->init(freq_hz);
}

void bm_hal_timer_stop(void) {
    if (BM_TIMER_DRV->stop) {
        BM_TIMER_DRV->stop();
    }
}

uint32_t bm_hal_timer_get_ticks(void) {
    if (!BM_TIMER_DRV->get_ticks) {
        return 0u;
    }
    return BM_TIMER_DRV->get_ticks();
}

uint32_t bm_hal_timer_get_freq(void) {
    if (!BM_TIMER_DRV->get_freq) {
        return 0u;
    }
    return BM_TIMER_DRV->get_freq();
}

void bm_hal_timer_set_callback(void (*cb)(void)) {
    if (BM_TIMER_DRV->set_callback) {
        BM_TIMER_DRV->set_callback(cb);
    }
}

uint16_t bm_hal_timer_clock_id_for_cpu(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return UINT16_MAX;
    }
    /*
     * 委托 bm_timestamp_clock_for_cpu() 执行实际映射，
     * 避免两处重复维护 clock_id 分配逻辑。
     */
    return bm_timestamp_clock_for_cpu(cpu);
}

uint32_t bm_hal_timer_get_ticks_on_cpu(uint32_t cpu) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (cpu == BM_CPU_THIS()) {
        return bm_hal_timer_get_ticks();
    }
#if defined(BM_NATIVE_SIM_TIMER_CPU_LOCAL)
    return bm_hal_timer_native_ticks_on_cpu(cpu);
#else
    (void)cpu;
    return 0u;
#endif
#else
    (void)cpu;
    return bm_hal_timer_get_ticks();
#endif
}
uint32_t bm_hal_timer_get_freq_on_cpu(uint32_t cpu) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (cpu == BM_CPU_THIS()) {
        return bm_hal_timer_get_freq();
    }
#if defined(BM_NATIVE_SIM_TIMER_CPU_LOCAL)
    return bm_hal_timer_native_freq_on_cpu(cpu);
#else
    (void)cpu;
    return 0u;
#endif
#else
    (void)cpu;
    return bm_hal_timer_get_freq();
#endif
}

uint32_t bm_hal_timer_clock_epoch_for_cpu(uint32_t cpu) {
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return UINT32_MAX;
    }
    /*
     * 非一致缓存平台：读取前 invalidate，确保读到最新 epoch。
     * 在缓存一致性硬件或 NOOP 平台上，invalidate 退化为 acquire fence。
     */
    if (!bm_hal_cache_is_noop()) {
        bm_hal_cache_invalidate(&s_clock_epoch[cpu],
                                (uint32_t)sizeof(s_clock_epoch[cpu]));
    }
    return bm_atomic_ipc_load_u32(&s_clock_epoch[cpu]);
}

bm_hal_timer_handle_t bm_hal_timer_for_cpu(uint32_t cpu) {
    bm_hal_timer_handle_t handle;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        handle.cpu = UINT8_MAX;
        handle.clock_id = UINT16_MAX;
        handle.clock_epoch = UINT32_MAX;
        return handle;
    }
    handle.cpu = (uint8_t)cpu;
    handle.clock_id = bm_hal_timer_clock_id_for_cpu(cpu);
    if (!bm_hal_cache_is_noop()) {
        bm_hal_cache_invalidate(&s_clock_epoch[cpu],
                                (uint32_t)sizeof(s_clock_epoch[cpu]));
    }
    handle.clock_epoch = bm_atomic_ipc_load_u32(&s_clock_epoch[cpu]);
    return handle;
}

void bm_hal_timer_bump_clock_epoch(uint32_t cpu) {
    if (cpu < BM_CONFIG_CPU_COUNT) {
        bm_atomic_ipc_inc_u32(&s_clock_epoch[cpu]);
        /*
         * 非一致缓存平台：自增后 clean，将新 epoch 写入共享内存。
         * 在缓存一致性硬件或 NOOP 平台上，clean 退化为 release fence。
         */
        if (!bm_hal_cache_is_noop()) {
            bm_hal_cache_clean(&s_clock_epoch[cpu],
                               (uint32_t)sizeof(s_clock_epoch[cpu]));
        }
    }
}

void bm_hal_timer_bump_all_clock_epochs(void) {
    uint32_t cpu;

    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        bm_hal_timer_bump_clock_epoch(cpu);
    }
}

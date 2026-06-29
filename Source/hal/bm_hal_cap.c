/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_cap.c
 * @brief HAL 能力查询默认实现
 *
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
#include "hal/bm_hal_cap.h"
#include "bm_config.h"
#include "bm/core/bm_cpu_local.h"
#include "hal/bm_hal_cache.h"

uint32_t bm_hal_cap_query(void) {
    uint32_t cap = 0u;

    /*
     * BM_HAL_CAP_CRITICAL_MASKS_STREAM_IRQ：
     * 由 port 层通过 BM_HAL_CRITICAL_MASKS_STREAM_IRQ 显式声明。
     * 不再从 BM_DRV_HAS_BACKEND 推断——存在后端 ≠ 一定能屏蔽 Stream IRQ。
     * 如 native_sim 后端提供临界区但不屏蔽 stream IRQ，不可置位此能力。
     */
#if defined(BM_HAL_CRITICAL_MASKS_STREAM_IRQ) && BM_HAL_CRITICAL_MASKS_STREAM_IRQ
    cap |= BM_HAL_CAP_CRITICAL_MASKS_STREAM_IRQ;
#endif

    if (!bm_hal_cache_is_noop()) {
        cap |= BM_HAL_CAP_STREAM_CACHE_MAINT;
    }
    return cap;
}

int bm_hal_cap_stream_profile_ok(void) {
    uint32_t cap = bm_hal_cap_query();
    uint32_t required = BM_HAL_CAP_CRITICAL_MASKS_STREAM_IRQ;

#if BM_CONFIG_HARD_RT_PROFILE && BM_CONFIG_ENABLE_STREAM
    if (!bm_hal_cache_is_noop()) {
        required |= BM_HAL_CAP_STREAM_CACHE_MAINT;
    }
#endif
    return (cap & required) == required ? 1 : 0;
}

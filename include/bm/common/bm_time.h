/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_time.h
 * @brief 毫秒到 HAL 定时器 tick 的公共换算
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_TIME_H
#define BM_TIME_H

#include "hal/bm_hal_timer.h"
#include "bm/common/bm_safety.h"
#include "bm/common/bm_types.h"

/**
 * @brief 将毫秒周期转换为 HAL 定时器 tick 数
 *
 * @param period_ms 周期（毫秒）
 * @param ticks_out 输出 tick 数
 * @return BM_OK 成功；BM_ERR_INVALID 参数或换算无效
 */
static inline int bm_time_ms_to_ticks(uint32_t period_ms, uint32_t *ticks_out) {
    uint32_t freq = bm_hal_timer_get_freq();
    uint32_t product = 0u;

    if (freq == 0u || !ticks_out) {
        return BM_ERR_INVALID;
    }
    if (bm_mul_u32_overflow(period_ms, freq, &product)) {
        return BM_ERR_INVALID;
    }
    *ticks_out = product / 1000u;
    if (*ticks_out == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

#endif /* BM_TIME_H */

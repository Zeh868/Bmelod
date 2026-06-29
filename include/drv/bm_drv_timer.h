/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_timer.h
 * @brief 系统定时器驱动 API（平台单例后端实现）
 */
#ifndef BM_DRV_TIMER_H
#define BM_DRV_TIMER_H

#include <stdint.h>

struct bm_timer_driver_api {
    int (*init)(uint32_t freq_hz);
    void (*stop)(void);
    uint32_t (*get_ticks)(void);
    uint32_t (*get_freq)(void);
    void (*set_callback)(void (*cb)(void)); /**< 周期性 tick 回调，按 init 的 freq_hz 触发 */
};

#ifdef BM_DRV_TIMER_API
extern const struct bm_timer_driver_api bm_drv_timer_api;
#endif

#endif /* BM_DRV_TIMER_H */

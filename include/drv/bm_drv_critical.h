/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_critical.h
 * @brief 临界区驱动 API（平台单例后端实现）
 */
#ifndef BM_DRV_CRITICAL_H
#define BM_DRV_CRITICAL_H

#include "hal/bm_hal_critical.h"

struct bm_critical_driver_api {
    bm_irq_state_t (*enter)(void);
    void (*exit)(bm_irq_state_t state);
    int (*in_isr)(void);
#if BM_HAL_HAS_PRIORITY_MASK
    bm_irq_state_t (*enter_below)(uint8_t threshold);
    void (*exit_below)(bm_irq_state_t previous_state);
#endif
};

#ifdef BM_DRV_CRITICAL_API
extern const struct bm_critical_driver_api bm_drv_critical_api;
#endif

#endif /* BM_DRV_CRITICAL_H */

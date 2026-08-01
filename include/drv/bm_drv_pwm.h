/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_pwm.h
 * @brief PWM 设备驱动 API
 * @maturity E1
 * @author Bmelod contributors
 * @version 1.0
 * @date 2026-08-01
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#ifndef BM_DRV_PWM_H
#define BM_DRV_PWM_H

#include "drv/bm_drv.h"
#include "hal/bm_hal_hrt.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

struct bm_hal_pwm;

struct bm_pwm_driver_api {
    int (*set_duty)(const struct bm_hal_pwm *dev, uint32_t phase, uint16_t duty);
    int (*enable_outputs)(const struct bm_hal_pwm *dev, int enable);
    int (*request_safe_state)(const struct bm_hal_pwm *dev);
    int (*bind_update)(const struct bm_hal_pwm *dev,
                       const bm_hal_hrt_binding_t *binding);
};

struct bm_hal_pwm {
    const struct bm_pwm_driver_api *api;
    const void                     *config;
};

#endif /* BM_DRV_PWM_H */

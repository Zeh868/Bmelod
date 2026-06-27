/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_pwm_sim.h
 * @brief 原生仿真 PWM 实例与测试辅助接口
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

#ifndef BM_HAL_PWM_SIM_H
#define BM_HAL_PWM_SIM_H

#include "bm_hal_pwm.h"

extern const bm_hal_pwm_t BM_HAL_PWM_SIM0;
extern const bm_hal_pwm_t BM_HAL_PWM_SIM1;
extern const bm_hal_pwm_t BM_HAL_PWM_SIM2;

void bm_hal_pwm_sim_fire_update(const bm_hal_pwm_t *pwm);
uint16_t bm_hal_pwm_sim_get_duty(const bm_hal_pwm_t *pwm, uint32_t phase);
int bm_hal_pwm_sim_outputs_enabled(const bm_hal_pwm_t *pwm);

#endif /* BM_HAL_PWM_SIM_H */

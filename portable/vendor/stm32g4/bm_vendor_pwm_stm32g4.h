/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_pwm_stm32g4.h
 * @brief STM32G474xB 三相互补 PWM（TIM1）实例声明
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 *
 */
#ifndef BM_VENDOR_PWM_STM32G4_H
#define BM_VENDOR_PWM_STM32G4_H

#include "bm_hal_pwm.h"

/** @brief M0 电机三相互补 PWM 实例（TIM1，NUCLEO-G474RE 默认引脚绑定）。 */
extern const bm_hal_pwm_t bm_hal_pwm_m0;

#endif /* BM_VENDOR_PWM_STM32G4_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_devices_native.h
 * @brief native_sim 后端实例出口（devices 聚合头 + default 别名）
 *
 * 经 `include/hal/bm_hal_devices.h` 由 pack 宏
 * `BM_HAL_DEVICES_HEADER="bm_hal_devices_native.h"` 引入；
 * 聚合 native 后端全部 `bm_hal_*` 实例声明（include 既有实例头，
 * 不重复 extern 声明，避免双份声明漂移），并提供
 * `bm_<class>_default` 首选实例别名。
 *
 * `bm_uart_default` / `bm_can_default` 为后端既有真实符号，不另起别名。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（P1 跨后端实例出口）
 *
 */
#ifndef BM_HAL_DEVICES_NATIVE_H
#define BM_HAL_DEVICES_NATIVE_H

#include "bm_hal_pwm_sim.h"
#include "bm_hal_adc_sim.h"
#include "bm_hal_encoder_sim.h"
#include "bm_hal_comp_sim.h"
#include "bm_hal_dma_stream_sim.h"
#include "bm_hal_gpio_native.h"
#include "bm_hal_hrtimer_native.h"
#include "bm_hal_uart_native.h"
#include "bm_hal_can_native.h"

/** @brief 首选 PWM 实例（多轴场景可用 BM_HAL_PWM_SIM1/2）。 */
#define bm_pwm_default         BM_HAL_PWM_SIM0
/** @brief 首选 ADC 实例。 */
#define bm_adc_default         BM_HAL_ADC_SIM0
/** @brief 首选编码器实例。 */
#define bm_encoder_default     BM_HAL_ENC_SIM0
/** @brief 首选比较器实例。 */
#define bm_comp_default        BM_HAL_COMP_SIM0
/** @brief 首选 DMA stream 实例。 */
#define bm_dma_stream_default  BM_HAL_DMA_SIM0
/** @brief 首选 GPIO 实例。 */
#define bm_gpio_default        bm_native_gpio
/** @brief 首选高精度 Timer 实例。 */
#define bm_hrtimer_default     bm_native_hrtimer0

#endif /* BM_HAL_DEVICES_NATIVE_H */

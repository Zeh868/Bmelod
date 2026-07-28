/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_gpio_native.h
 * @brief native_sim GPIO 后端测试辅助接口
 *
 * 仅供 native_sim 单元测试使用，用于设置 pin 电平与手动触发 EXTI。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim GPIO 测试辅助
 */
#ifndef BM_HAL_GPIO_NATIVE_H
#define BM_HAL_GPIO_NATIVE_H

#include "hal/bm_hal_gpio.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief native_sim GPIO 设备实例。 */
extern const bm_hal_gpio_t bm_native_gpio;

/**
 * @brief 重置所有 native_sim pin 状态（测试用）。
 */
void bm_hal_gpio_native_reset(void);

/**
 * @brief 设置 pin 电平（测试用）。
 */
void bm_hal_gpio_native_set_pin(uint32_t pin, int value);

/**
 * @brief 手动触发指定 pin 的 EXTI 回调（测试用）。
 */
void bm_hal_gpio_native_fire_exti(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_GPIO_NATIVE_H */

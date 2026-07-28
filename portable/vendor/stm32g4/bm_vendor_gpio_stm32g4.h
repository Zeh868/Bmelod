/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_gpio_stm32g4.h
 * @brief STM32G474xB GPIO 设备声明（bm_drv_gpio 契约，全 GPIO 口，含 EXTI）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 * 2026-07-28       1.1            zeh            增加 EXTI 平台配置结构与已实现说明
 *
 */
#ifndef BM_VENDOR_GPIO_STM32G4_H
#define BM_VENDOR_GPIO_STM32G4_H

#include "bm_hal_gpio.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief STM32G4 GPIO EXTI 默认 NVIC 优先级（config 为 NULL 时使用）。 */
#define BM_STM32G4_GPIO_EXTI_IRQ_PRIORITY_DEFAULT  5u

/**
 * @brief STM32G4 GPIO 平台配置（可选，经 bm_hal_gpio.config 传入）。
 */
typedef struct {
    uint32_t irq_priority; /**< EXTI NVIC 优先级；0 表示使用默认 5 */
} bm_gpio_stm32g4_config_t;

/** @brief STM32G4 全芯片 GPIO 设备（pin 编码 (port<<4)|num，见 bm_drv_gpio.h）。 */
extern const bm_hal_gpio_t bm_stm32g4_gpio;

#ifdef __cplusplus
}
#endif

#endif /* BM_VENDOR_GPIO_STM32G4_H */

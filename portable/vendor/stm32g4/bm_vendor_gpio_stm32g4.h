/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_gpio_stm32g4.h
 * @brief STM32G474xB GPIO 设备声明（bm_drv_gpio 契约，全 GPIO 口）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 *
 */
#ifndef BM_VENDOR_GPIO_STM32G4_H
#define BM_VENDOR_GPIO_STM32G4_H

#include "bm_hal_gpio.h"

/** @brief STM32G4 全芯片 GPIO 设备（pin 编码 (port<<4)|num，见 bm_drv_gpio.h）。 */
extern const bm_hal_gpio_t bm_stm32g4_gpio;

#endif /* BM_VENDOR_GPIO_STM32G4_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_devices_stm32g4.h
 * @brief STM32G4 后端实例出口（devices 聚合头 + default 别名）
 * @maturity E1
 *
 * 经 `include/hal/bm_hal_devices.h` 由 pack 宏
 * `BM_HAL_DEVICES_HEADER="bm_hal_devices_stm32g4.h"` 引入；
 * 聚合 STM32G4 后端全部 `bm_hal_*` 实例声明（include 既有实例头，
 * 不重复 extern 声明），并提供 `bm_<class>_default` 首选实例别名。
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
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_DEVICES_STM32G4_H
#define BM_HAL_DEVICES_STM32G4_H

#include "bm_vendor_pwm_stm32g4.h"
#include "bm_vendor_adc_stm32g4.h"
#include "bm_vendor_encoder_stm32g4.h"
#include "bm_vendor_comp_stm32g4.h"
#include "bm_vendor_gpio_stm32g4.h"
#include "bm_vendor_hrtimer_stm32g4.h"
#include "bm_vendor_spi_stm32g4.h"
#include "bm_vendor_can_stm32g4.h"
#include "bm_vendor_uart_dev_stm32g4.h"
#include "bm_vendor_dma_usart2_rx_stm32g4.h"

/** @brief 首选 PWM 实例。 */
#define bm_pwm_default         bm_hal_pwm_m0
/** @brief 首选 ADC 实例。 */
#define bm_adc_default         bm_hal_adc_m0
/** @brief 首选编码器实例。 */
#define bm_encoder_default     bm_hal_encoder_m0
/** @brief 首选比较器实例。 */
#define bm_comp_default        bm_hal_comp_m0
/** @brief 首选 GPIO 实例。 */
#define bm_gpio_default        bm_stm32g4_gpio
/** @brief 首选高精度 Timer 实例。 */
#define bm_hrtimer_default     bm_stm32g4_hrtimer0
/** @brief 首选 SPI 实例。 */
#define bm_spi_default         bm_stm32g4_spi1
/** @brief 首选 CAN 实例。 */
#define bm_can_default         bm_stm32g4_can1
/** @brief 首选 UART 实例（USART2 设备模型）。 */
#define bm_uart_default        bm_stm32g4_uart_dev_usart2
/** @brief 首选 DMA stream 实例（USART2 RX 环形通道）。 */
#define bm_dma_stream_default  bm_stm32g4_usart2_rx_dma

#endif /* BM_HAL_DEVICES_STM32G4_H */

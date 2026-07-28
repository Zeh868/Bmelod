/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_usart3_stm32g4.h
 * @brief STM32G4 USART3 设备实例配置与声明
 *
 * App 通过 `bm_usart3_stm32g4_config_t` 指定引脚/DMA/IRQ/DE；
 * Bmelod 不固定 USART3 与具体产品引脚。
 *
 * 当前实现覆盖 IDLE + DMA TX/RX 路径；RX 使用 ring buffer，由上层通过
 * `bm_hal_uart_set_rx_buffer()` 提供。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 USART3 后端
 */
#ifndef BM_VENDOR_USART3_STM32G4_H
#define BM_VENDOR_USART3_STM32G4_H

#include "hal/bm_hal_uart.h"

#include <stdint.h>

#include "stm32g4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STM32G4 USART3 平台配置。
 *
 * App 静态分配并填写；init 时由后端校验合法性。
 */
typedef struct {
    uint32_t baud;            /**< 波特率 */
    uint32_t parity;          /**< BM_UART_PARITY_* */
    uint32_t stop_bits;       /**< BM_UART_STOPBITS_* */
    uint32_t data_bits;       /**< BM_UART_DATABITS_* */

    uint32_t tx_pin;          /**< TX 引脚号（GPIOB 默认） */
    uint32_t rx_pin;          /**< RX 引脚号（GPIOB 默认） */
    uint32_t gpio_af;         /**< GPIO 复用功能号 */

    uint32_t tx_dma_ch;       /**< TX DMA 通道号（1-based） */
    uint32_t tx_dma_req;      /**< TX DMAMUX 请求号 */
    uint32_t rx_dma_ch;       /**< RX DMA 通道号（1-based） */
    uint32_t rx_dma_req;      /**< RX DMAMUX 请求号 */

    IRQn_Type usart_irqn;     /**< USART 全局中断 */
    IRQn_Type tx_dma_irqn;    /**< TX DMA 完成中断 */
    IRQn_Type rx_dma_irqn;    /**< RX DMA 完成/半满中断 */

    uint32_t irq_priority;       /**< USART NVIC 优先级 */
    uint32_t tx_dma_irq_priority;/**< TX DMA NVIC 优先级 */
    uint32_t rx_dma_irq_priority;/**< RX DMA NVIC 优先级 */
} bm_usart3_stm32g4_config_t;

/** @brief 默认 USART3 设备实例（PB10/PB11，DMA1_CH4/CH5）。 */
extern const bm_hal_uart_t bm_stm32g4_usart3;

#ifdef __cplusplus
}
#endif

#endif /* BM_VENDOR_USART3_STM32G4_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_dma_usart2_rx_stm32g4.h
 * @brief STM32G474xB USART2 RX DMA 块流设备声明（bm_drv_dma_stream 契约）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（UART RX DMA）
 *
 */
#ifndef BM_VENDOR_DMA_USART2_RX_STM32G4_H
#define BM_VENDOR_DMA_USART2_RX_STM32G4_H

#include "bm_hal_dma_stream.h"

/**
 * @brief USART2 RX DMA 块流设备（DMA1 循环模式 + 半满/全满回调）。
 *
 * 使用前置：USART2 已由 bm_stm32g4_uart_dev_usart2 初始化（时钟/GPIO/波特率），
 * 本设备只接管 RX DMA 通路（UART TX DMA 未实现，登记为缺口）。
 */
extern const bm_hal_dma_stream_t bm_stm32g4_usart2_rx_dma;

#endif /* BM_VENDOR_DMA_USART2_RX_STM32G4_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_usart3_stm32g4.h
 * @brief STM32G4 USART3 设备实例配置与声明
 * @maturity E1
 *
 * App 通过 `bm_usart3_stm32g4_config_t` 指定引脚/DMA/IRQ/DE；
 * Bmelod 不固定 USART3 与具体产品引脚。
 *
 * 当前实现覆盖 IDLE + DMA TX/RX 路径：
 * - RX 使用 DMA circular + 软件读指针；IDLE / RX_FULL 触发帧回调；HT 只维护
 *   写指针，不交付 FRAME_END。
 * - TX 使用 DMA normal + USART TC：调用 `bm_hal_uart_send()` 须保证 `data` 在
 *   DMA 传输及 UART 移位器清空期间保持有效（至 `tx_complete_callback` 触发或
 *   `bm_hal_uart_flush()` 返回）。发送期间再次调用 `send()` 返回 `BM_ERR_BUSY`。
 * - `kernel_clock_hz==0` 时波特率按 PCLK1 推算；改过 `CCIPR.USARTxSEL` 时须由
 *   Board 填入实际内核时钟。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 USART3 后端
 * 2026-07-28       1.1            zeh            HT 不交付 FRAME_END；kernel_clock_hz
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
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

    /**
     * @brief USART 内核时钟（Hz）；0=假定等于 PCLK1（CCIPR 默认）。
     *
     * 板级若改过 `RCC_CCIPR.USART3SEL`，须填入实际内核时钟，否则波特率错误。
     */
    uint32_t kernel_clock_hz;

    uint32_t tx_port;         /**< TX GPIO 端口（0=A,1=B,2=C,3=D,4=E,5=F,6=G） */
    uint32_t tx_pin;          /**< TX 引脚号（0..15） */
    uint32_t rx_port;         /**< RX GPIO 端口（0=A,1=B,2=C,3=D,4=E,5=F,6=G） */
    uint32_t rx_pin;          /**< RX 引脚号（0..15） */
    uint32_t gpio_af;         /**< GPIO 复用功能号 */

    uint32_t tx_dma_ctrl;     /**< TX DMA 控制器（1=DMA1, 2=DMA2） */
    uint32_t tx_dma_ch;       /**< TX DMA 通道号（1-based） */
    uint32_t tx_dma_req;      /**< TX DMAMUX 请求号 */
    uint32_t rx_dma_ctrl;     /**< RX DMA 控制器（1=DMA1, 2=DMA2） */
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

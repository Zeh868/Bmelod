/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_uart_dev_stm32g4.h
 * @brief STM32G474xB USART2 设备实例声明（bm_drv_uart 实例契约）
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 * 2026-07-28       1.1            zeh            增加 kernel_clock_hz（0=假定 PCLK1）
 *
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#ifndef BM_VENDOR_UART_DEV_STM32G4_H
#define BM_VENDOR_UART_DEV_STM32G4_H

#include "bm_hal_uart.h"

/**
 * @brief USART2 设备运行时配置（bm_hal_uart_dev_init 的 config 入参；
 *        NULL 时全部取 instances 宏默认值）。
 */
typedef struct bm_stm32g4_uart_dev_config {
    uint32_t baud;              /**< 波特率；0 = BM_STM32G4_USART2_BAUD */
    uint8_t  single_wire;       /**< 非零：HDSEL 单线半双工（仅 TX 脚）；0xff 内为“未指定”语义见 .c */
    /**
     * @brief USART 内核时钟（Hz）；0=假定等于 PCLK1（CCIPR 默认）。
     *
     * 板级若改过 `RCC_CCIPR.USART2SEL`，须填入实际内核时钟。
     */
    uint32_t kernel_clock_hz;
} bm_stm32g4_uart_dev_config_t;

/** @brief STM32G4 USART2 设备实例（TMC2209 等用；console 仍走 LPUART1 单例）。 */
extern const bm_hal_uart_t bm_stm32g4_uart_dev_usart2;

#endif /* BM_VENDOR_UART_DEV_STM32G4_H */

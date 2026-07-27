/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_uart.h
 * @brief UART 设备驱动 API（统一实例模型）
 *
 * 全实例化：设备 {api, config} + 实例 vtable，无单例全局符号。
 * 各后端导出一个默认控制台设备 bm_uart_default（声明见 bm_hal_uart.h），
 * 其余 UART（TMC2209/RS485 等）由 vendor 以同型设备导出。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布（单例契约）
 * 2026-07-27       2.0            zeh            打破式全实例化：删除单例全局符号约定与
 *                                                接口批 1 的 bm_uart_dev_api/bm_hal_uart_dev，
 *                                                统一为 {api, config} 设备模型
 *
 */
#ifndef BM_DRV_UART_H
#define BM_DRV_UART_H

#include <stddef.h>
#include <stdint.h>

struct bm_hal_uart;

struct bm_uart_driver_api {
    int (*init)(const struct bm_hal_uart *dev, void *config);
    int (*send)(const struct bm_hal_uart *dev,
                const uint8_t *data, size_t len);
    size_t (*recv)(const struct bm_hal_uart *dev,
                   uint8_t *data, size_t max_len);
    void (*set_rx_callback)(const struct bm_hal_uart *dev,
                            void (*cb)(uint8_t c));
};

struct bm_hal_uart {
    const struct bm_uart_driver_api *api;
    const void                      *config;
};

#endif /* BM_DRV_UART_H */

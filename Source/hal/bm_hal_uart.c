/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_uart.c
 * @brief UART HAL 分发层（统一实例模型：契约 → driver API）
 *
 * 设备未绑定后端（api 为 NULL）时返回 BM_ERR_NOT_INIT / 0。
 * hard RT 剖面下阻塞收发与 RX 用户回调均 fail-closed。
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布（单例分发）
 * 2026-06-15       1.1            zeh            hard RT 禁止阻塞 UART 路径
 * 2026-07-27       1.2            zeh            追加设备实例分发（过渡形态）
 * 2026-07-27       2.0            zeh            打破式全实例化：删除单例分发与过渡形态，
 *                                                统一 encoder 式设备分发
 *
 */
#include "bm_hal_uart.h"
#include "bm_config.h"
#include "bm_types.h"

int bm_hal_uart_init(const bm_hal_uart_t *uart, void *config) {
    if (!uart || !uart->api || !uart->api->init) {
        return BM_ERR_NOT_INIT;
    }
    return uart->api->init(uart, config);
}

int bm_hal_uart_send(const bm_hal_uart_t *uart, const uint8_t *data, size_t len) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    (void)data;
    (void)len;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!uart || !uart->api || !uart->api->send) {
        return BM_ERR_NOT_INIT;
    }
    if (!data && len > 0u) {
        return BM_ERR_INVALID;
    }
    return uart->api->send(uart, data, len);
#endif
}

size_t bm_hal_uart_recv(const bm_hal_uart_t *uart, uint8_t *data, size_t max_len) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    (void)data;
    (void)max_len;
    return 0u;
#else
    if (!uart || !uart->api || !uart->api->recv) {
        return 0u;
    }
    if (!data || max_len == 0u) {
        return 0u;
    }
    return uart->api->recv(uart, data, max_len);
#endif
}

void bm_hal_uart_set_rx_callback(const bm_hal_uart_t *uart, void (*cb)(uint8_t c)) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    (void)cb;
#else
    if (!uart || !uart->api || !uart->api->set_rx_callback) {
        return;
    }
    uart->api->set_rx_callback(uart, cb);
#endif
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_uart.c
 * @brief UART HAL 分发层（统一实例模型：契约 → driver API）
 *
 * 设备未绑定后端（api 为 NULL）时返回 BM_ERR_NOT_INIT / 0。
 * hard RT 剖面下阻塞收发与 RX 用户回调均 fail-closed。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 3.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布（单例分发）
 * 2026-06-15       1.1            zeh            hard RT 禁止阻塞 UART 路径
 * 2026-07-27       2.0            zeh            打破式全实例化，统一 encoder 式设备分发
 * 2026-07-28       3.0            zeh            接口批 1 扩展 IDLE/环形缓冲/错误统计分发
 * 2026-08-01       3.0            Codex           补全 Doxygen 合规注释
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

int bm_hal_uart_abort(const bm_hal_uart_t *uart) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!uart || !uart->api || !uart->api->abort) {
        return BM_ERR_NOT_INIT;
    }
    return uart->api->abort(uart);
#endif
}

int bm_hal_uart_flush(const bm_hal_uart_t *uart) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!uart || !uart->api || !uart->api->flush) {
        return BM_ERR_NOT_INIT;
    }
    return uart->api->flush(uart);
#endif
}

int bm_hal_uart_set_tx_complete_callback(const bm_hal_uart_t *uart,
                                         bm_uart_tx_complete_callback_t cb,
                                         void *user) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    (void)cb;
    (void)user;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!uart || !uart->api || !uart->api->set_tx_complete_callback) {
        return BM_ERR_NOT_INIT;
    }
    return uart->api->set_tx_complete_callback(uart, cb, user);
#endif
}

int bm_hal_uart_set_rx_frame_callback(const bm_hal_uart_t *uart,
                                      bm_uart_rx_frame_callback_t cb,
                                      void *user) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    (void)cb;
    (void)user;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!uart || !uart->api || !uart->api->set_rx_frame_callback) {
        return BM_ERR_NOT_INIT;
    }
    return uart->api->set_rx_frame_callback(uart, cb, user);
#endif
}

int bm_hal_uart_set_rx_buffer(const bm_hal_uart_t *uart,
                              uint8_t *buf, size_t len) {
#if BM_CONFIG_HARD_RT_PROFILE
    (void)uart;
    (void)buf;
    (void)len;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (!uart || !uart->api || !uart->api->set_rx_buffer) {
        return BM_ERR_NOT_INIT;
    }
    return uart->api->set_rx_buffer(uart, buf, len);
#endif
}

int bm_hal_uart_get_stats(const bm_hal_uart_t *uart, bm_uart_stats_t *stats) {
    if (!uart || !uart->api || !uart->api->get_stats) {
        return BM_ERR_NOT_INIT;
    }
    if (!stats) {
        return BM_ERR_INVALID;
    }
    return uart->api->get_stats(uart, stats);
}

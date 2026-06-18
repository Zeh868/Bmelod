/**
 * @file bm_drv_uart.h
 * @brief UART 驱动 API（平台单例后端实现）
 */
#ifndef BM_DRV_UART_H
#define BM_DRV_UART_H

#include <stddef.h>
#include <stdint.h>

struct bm_uart_driver_api {
    int (*init)(void *config);
    int (*send)(const uint8_t *data, size_t len);
    size_t (*recv)(uint8_t *data, size_t max_len);
    void (*set_rx_callback)(void (*cb)(uint8_t c));
};

#ifdef BM_DRV_UART_API
extern const struct bm_uart_driver_api bm_drv_uart_api;
#endif

#endif /* BM_DRV_UART_H */

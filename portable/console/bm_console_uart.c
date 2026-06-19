/**
 * @file bm_console_uart.c
 * @brief Console UART 后端（转发 bm_hal_uart）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 *
 */
#include "bm_types.h"
#include "hal/bm_hal_uart.h"

static int g_uart_inited;

/**
 * @brief 初始化 UART 后端
 */
int bm_console_uart_init(void) {
    int rc;

    if (g_uart_inited) {
        return BM_OK;
    }
    rc = bm_hal_uart_init(NULL);
    if (rc == BM_OK || rc == BM_ERR_NOT_INIT) {
        g_uart_inited = 1;
        return BM_OK;
    }
    return rc;
}

/**
 * @brief 经 UART HAL 写出
 */
int bm_console_uart_write(const uint8_t *data, size_t len) {
    if (!data || len == 0u) {
        return BM_ERR_INVALID;
    }
    return bm_hal_uart_send(data, len);
}

/**
 * @brief 经 UART HAL 非阻塞读
 */
size_t bm_console_uart_read(uint8_t *data, size_t max_len) {
    if (!data || max_len == 0u) {
        return 0u;
    }
    return bm_hal_uart_recv(data, max_len);
}

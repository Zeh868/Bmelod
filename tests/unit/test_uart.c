/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_uart.c
 * @brief UART HAL/drv 与 native_sim 多实例后端单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 UART 多实例/IDLE 单测
 * 2026-07-28       1.1            zeh            补 reset 全量复位与 TX 缓冲满
 *                                             返回 BM_ERR_BUSY 用例
 */
#include "unity.h"
#include "hal/bm_hal_uart.h"
#include "bm_hal_uart_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <string.h>

static uint32_t s_frame_event;
static size_t   s_frame_len;
static uint32_t s_tx_complete_count;

void setUp(void) {
    bm_hal_uart_native_reset();
    s_frame_event = 0u;
    s_frame_len = 0u;
    s_tx_complete_count = 0u;
}

void tearDown(void) {
}

static void test_uart_tx_complete_cb(const bm_hal_uart_t *dev, void *user) {
    (void)dev;
    (void)user;
    s_tx_complete_count++;
}

static void test_uart_rx_frame_cb(const bm_hal_uart_t *dev, uint32_t event,
                                  size_t len, void *user) {
    (void)dev;
    (void)user;
    s_frame_event = event;
    s_frame_len = len;
}

static void test_uart_init_and_send_default(void) {
    uint8_t data[] = "hello";

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_uart_default, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_send(&bm_uart_default, data, sizeof(data) - 1u));
}

static void test_uart_rx_buffer_and_idle(void) {
    uint8_t rx_buf[64];
    uint8_t rx_data[8];

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_set_rx_buffer(&bm_native_uart1, rx_buf, sizeof(rx_buf)));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_set_rx_frame_callback(&bm_native_uart1,
                                          test_uart_rx_frame_cb, NULL));

    /* 注入 RX 字节 */
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0x01);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0x02);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0x03);

    /* recv 可立即读取 */
    const uint8_t expected[] = { 0x01, 0x02, 0x03 };

    TEST_ASSERT_EQUAL(3u,
        bm_hal_uart_recv(&bm_native_uart1, rx_data, sizeof(rx_data)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, rx_data, 3u);

    /* 再次注入并触发 IDLE */
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0xAA);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0xBB);
    bm_hal_uart_native_fire_idle(&bm_native_uart1);
    TEST_ASSERT_EQUAL(BM_UART_EVT_IDLE, s_frame_event);
    TEST_ASSERT_EQUAL(2u, s_frame_len);
}

static void test_uart_error_stats(void) {
    bm_uart_stats_t stats;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));
    bm_hal_uart_native_inject_error(&bm_native_uart1,
        BM_UART_ERR_FRAMING | BM_UART_ERR_PARITY);

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_get_stats(&bm_native_uart1, &stats));
    TEST_ASSERT_EQUAL(1u, stats.rx_framing_count);
    TEST_ASSERT_EQUAL(1u, stats.rx_parity_count);
}

static void test_uart_tx_complete_callback(void) {
    uint8_t data[] = { 0x11, 0x22 };

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_set_tx_complete_callback(&bm_native_uart1,
                                             test_uart_tx_complete_cb, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_send(&bm_native_uart1, data, sizeof(data)));
    TEST_ASSERT_EQUAL(1u, s_tx_complete_count);
}

static void test_uart_multi_instance(void) {
    uint8_t rx_buf0[32];
    uint8_t rx_buf1[32];

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_uart_default, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_set_rx_buffer(&bm_uart_default, rx_buf0, sizeof(rx_buf0)));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_set_rx_buffer(&bm_native_uart1, rx_buf1, sizeof(rx_buf1)));

    bm_hal_uart_native_put_rx(&bm_uart_default, 0x12);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0x34);

    TEST_ASSERT_EQUAL(1u,
        bm_hal_uart_recv(&bm_uart_default, rx_buf0, sizeof(rx_buf0)));
    TEST_ASSERT_EQUAL(0x12, rx_buf0[0]);
    TEST_ASSERT_EQUAL(1u,
        bm_hal_uart_recv(&bm_native_uart1, rx_buf1, sizeof(rx_buf1)));
    TEST_ASSERT_EQUAL(0x34, rx_buf1[0]);
}

static void test_uart_no_backend(void) {
    const bm_hal_uart_t no_backend = { NULL, NULL };
    bm_uart_stats_t stats;
    uint8_t b = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_uart_init(&no_backend, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_send(&no_backend, &b, 1u));
    TEST_ASSERT_EQUAL(0u, bm_hal_uart_recv(&no_backend, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_abort(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_flush(&no_backend));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_set_tx_complete_callback(&no_backend, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_set_rx_frame_callback(&no_backend, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_set_rx_buffer(&no_backend, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
        bm_hal_uart_get_stats(&no_backend, &stats));
}

static uint32_t s_rx_byte_count;

static void test_uart_rx_byte_cb(uint8_t c) {
    (void)c;
    s_rx_byte_count++;
}

static void test_uart_reset_clears_all(void) {
    uint8_t data[] = { 0x55 };
    bm_uart_stats_t stats;

    s_rx_byte_count = 0u;
    s_tx_complete_count = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_set_tx_complete_callback(&bm_native_uart1,
                                             test_uart_tx_complete_cb, NULL));
    bm_hal_uart_set_rx_callback(&bm_native_uart1, test_uart_rx_byte_cb);
    bm_hal_uart_native_inject_error(&bm_native_uart1, BM_UART_ERR_FRAMING);

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_send(&bm_native_uart1, data, sizeof(data)));
    TEST_ASSERT_EQUAL(1u, s_tx_complete_count);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0x01);
    TEST_ASSERT_EQUAL(1u, s_rx_byte_count);

    /* reset 应全量复位：回调、统计、错误标志、TX 缓冲全部清除 */
    bm_hal_uart_native_reset();

    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_get_stats(&bm_native_uart1, &stats));
    TEST_ASSERT_EQUAL(0u, stats.tx_count);
    TEST_ASSERT_EQUAL(0u, stats.rx_count);
    TEST_ASSERT_EQUAL(0u, stats.rx_framing_count);
    TEST_ASSERT_EQUAL(0u, stats.last_errors);
    TEST_ASSERT_EQUAL(0u, bm_hal_uart_native_tx_count());

    /* 回调已清除：重新 init 后 send/put_rx 不再触发 */
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_send(&bm_native_uart1, data, sizeof(data)));
    TEST_ASSERT_EQUAL(1u, s_tx_complete_count);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0x02);
    TEST_ASSERT_EQUAL(1u, s_rx_byte_count);
}

static void test_uart_tx_buf_full_busy(void) {
    uint8_t data[128] = { 0 };

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&bm_native_uart1, NULL));

    /* TX 测试缓冲区共 256 字节：写满后再发送返回 BM_ERR_BUSY 且不落盘 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_send(&bm_native_uart1, data, sizeof(data)));
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_uart_send(&bm_native_uart1, data, sizeof(data)));
    TEST_ASSERT_EQUAL(256u, bm_hal_uart_native_tx_count());
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
        bm_hal_uart_send(&bm_native_uart1, data, 1u));
    TEST_ASSERT_EQUAL(256u, bm_hal_uart_native_tx_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_uart_init_and_send_default);
    RUN_TEST(test_uart_rx_buffer_and_idle);
    RUN_TEST(test_uart_error_stats);
    RUN_TEST(test_uart_tx_complete_callback);
    RUN_TEST(test_uart_multi_instance);
    RUN_TEST(test_uart_reset_clears_all);
    RUN_TEST(test_uart_tx_buf_full_busy);
    RUN_TEST(test_uart_no_backend);
    return UNITY_END();
}

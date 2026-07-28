/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_rs485.c
 * @brief RS485 包装组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 RS485 单测
 */
#include "unity.h"
#include "bm/component/bm_rs485.h"
#include "hal/bm_hal_gpio.h"
#include "bm_hal_gpio_native.h"
#include "bm_hal_uart_native.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <string.h>

#define TEST_DE_PIN  BM_GPIO_PIN_ENCODE(1, 0)  /* PB0 */

static uint8_t s_rx_frame[BM_RS485_MAX_FRAME_LEN];
static size_t  s_rx_len;
static uint32_t s_error_flags;

void setUp(void) {
    bm_hal_gpio_native_reset();
    bm_hal_uart_native_reset();
    s_rx_len = 0u;
    s_error_flags = 0u;
}

void tearDown(void) {
}

static void test_rs485_frame_cb(const bm_rs485_t *rs485,
                                const uint8_t *data, size_t len, void *user) {
    (void)rs485;
    (void)user;
    if (len <= sizeof(s_rx_frame)) {
        (void)memcpy(s_rx_frame, data, len);
        s_rx_len = len;
    }
}

static void test_rs485_error_cb(const bm_rs485_t *rs485,
                                uint32_t error, void *user) {
    (void)rs485;
    (void)user;
    s_error_flags |= error;
}

static void test_rs485_init_de_direction(void) {
    bm_rs485_t rs485;
    int de_level;

    (void)memset(&rs485, 0, sizeof(rs485));
    rs485.config.uart = &bm_native_uart1;
    rs485.config.de_gpio = &bm_native_gpio;
    rs485.config.de_pin = TEST_DE_PIN;
    rs485.config.de_active_high = 1;
    rs485.config.filter_echo = 1;

    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_init(&rs485));
    TEST_ASSERT_EQUAL(BM_RS485_DIR_RX, bm_rs485_dir(&rs485));

    /* 高有效 DE，接收方向应为低 */
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_gpio_read(&bm_native_gpio, TEST_DE_PIN, &de_level));
    TEST_ASSERT_EQUAL(0, de_level);
}

static void test_rs485_send_and_receive(void) {
    bm_rs485_t rs485;
    uint8_t frame[] = { 0x01, 0x02, 0x03, 0x04 };

    (void)memset(&rs485, 0, sizeof(rs485));
    rs485.config.uart = &bm_native_uart1;
    rs485.config.de_gpio = &bm_native_gpio;
    rs485.config.de_pin = TEST_DE_PIN;
    rs485.config.de_active_high = 1;
    rs485.config.filter_echo = 1;
    rs485.resources.frame_rx_cb = test_rs485_frame_cb;

    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_init(&rs485));

    /* 发送；native TX 完成回调立即触发 */
    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_send(&rs485, frame, sizeof(frame)));

    /* 模拟总线回显：注入与发送完全相同的内容 */
    bm_hal_uart_native_put_rx_data(&bm_native_uart1, frame, sizeof(frame));

    /* poll 切回 RX */
    bm_rs485_poll(&rs485);
    TEST_ASSERT_EQUAL(BM_RS485_DIR_RX, bm_rs485_dir(&rs485));

    /* 触发 IDLE：回显应被过滤，形成空帧 */
    bm_hal_uart_native_fire_idle(&bm_native_uart1);
    TEST_ASSERT_EQUAL(0u, s_rx_len);

    /* 从外部注入 RX 字节并触发 IDLE */
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0xAA);
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0xBB);
    bm_hal_uart_native_fire_idle(&bm_native_uart1);
    TEST_ASSERT_EQUAL(2u, s_rx_len);
    TEST_ASSERT_EQUAL(0xAA, s_rx_frame[0]);
    TEST_ASSERT_EQUAL(0xBB, s_rx_frame[1]);
}

static void test_rs485_collision_detection(void) {
    bm_rs485_t rs485;
    uint8_t frame[] = { 0x11, 0x22 };

    (void)memset(&rs485, 0, sizeof(rs485));
    rs485.config.uart = &bm_native_uart1;
    rs485.config.de_gpio = &bm_native_gpio;
    rs485.config.de_pin = TEST_DE_PIN;
    rs485.config.de_active_high = 1;
    rs485.config.filter_echo = 0; /* 关闭回显过滤，便于模拟冲突 */
    rs485.resources.error_cb = test_rs485_error_cb;

    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_init(&rs485));

    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_send(&rs485, frame, sizeof(frame)));
    /* 发送期间注入外部字节，应触发冲突 */
    bm_hal_uart_native_put_rx(&bm_native_uart1, 0xFF);
    bm_hal_uart_native_fire_idle(&bm_native_uart1);

    TEST_ASSERT_EQUAL(BM_RS485_ERR_COLLISION, s_error_flags);
}

static void test_rs485_stats(void) {
    bm_rs485_t rs485;
    bm_rs485_stats_t stats;
    uint8_t frame[] = { 0x01, 0x02 };

    (void)memset(&rs485, 0, sizeof(rs485));
    rs485.config.uart = &bm_native_uart1;
    rs485.config.de_gpio = &bm_native_gpio;
    rs485.config.de_pin = TEST_DE_PIN;
    rs485.config.de_active_high = 1;

    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_init(&rs485));
    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_send(&rs485, frame, sizeof(frame)));

    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_get_stats(&rs485, &stats));
    TEST_ASSERT_EQUAL(1u, stats.tx_frame_count);
    TEST_ASSERT_EQUAL(sizeof(frame), stats.tx_byte_count);
}

static void test_rs485_validate_config(void) {
    bm_rs485_config_t cfg;

    (void)memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_rs485_validate_config(&cfg));

    cfg.uart = &bm_native_uart1;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_rs485_validate_config(&cfg));

    cfg.hardware_de = 1;
    TEST_ASSERT_EQUAL(BM_OK, bm_rs485_validate_config(&cfg));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rs485_init_de_direction);
    RUN_TEST(test_rs485_send_and_receive);
    RUN_TEST(test_rs485_collision_detection);
    RUN_TEST(test_rs485_stats);
    RUN_TEST(test_rs485_validate_config);
    return UNITY_END();
}

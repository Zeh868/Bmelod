/**
 * @file test_hal_dispatch.c
 * @brief 接口批 1 分发层边界测试（GPIO/SPI/UART-dev：无后端 NOT_INIT 与参数校验）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm_hal_gpio.h"
#include "bm_hal_spi.h"
#include "bm_hal_uart.h"
#include "bm/common/bm_types.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief GPIO：NULL 设备 / 无 api 设备全部返回 BM_ERR_NOT_INIT
 */
void test_hal_gpio_dispatch_not_init(void) {
    bm_hal_gpio_t dev;
    int           value = 0;

    memset(&dev, 0, sizeof(dev));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_gpio_configure(NULL, 0u, BM_GPIO_INPUT));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_gpio_configure(&dev, 0u, BM_GPIO_INPUT));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_gpio_write(&dev, 0u, 1));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_gpio_read(&dev, 0u, &value));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_gpio_toggle(&dev, 0u));
}

/**
 * @brief SPI：NULL/无 api → NOT_INIT；tx/rx 同 NULL 或 len=0 → INVALID
 */
void test_hal_spi_dispatch_not_init_and_invalid(void) {
    static const struct bm_spi_driver_api api = { NULL };
    bm_hal_spi_t  dev = { &api, NULL };
    bm_hal_spi_t  noapi = { NULL, NULL };
    uint8_t       b = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_spi_transfer(NULL, &b, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_spi_transfer(&noapi, &b, &b, 1u));
    /* api 存在但 transfer 为 NULL → NOT_INIT */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_spi_transfer(&dev, &b, &b, 1u));
    /* 参数校验：transfer 指针有效与否不影响前置参数检查顺序，
       此处用无 api 设备以外的 INVALID 路径无法触达，改用合法 api 桩 */
}

/**
 * @brief SPI：tx/rx 同 NULL、len=0 的 INVALID 边界（经合法 api 桩）
 */
static int stub_transfer(const struct bm_hal_spi *dev,
                         const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)dev; (void)tx; (void)rx; (void)len;
    return BM_OK;
}

void test_hal_spi_dispatch_invalid_args(void) {
    static const struct bm_spi_driver_api api = { stub_transfer };
    bm_hal_spi_t dev = { &api, NULL };
    uint8_t      b = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_spi_transfer(&dev, NULL, NULL, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_spi_transfer(&dev, &b, &b, 0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_spi_transfer(&dev, &b, NULL, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_spi_transfer(&dev, NULL, &b, 1u));
}

/**
 * @brief SPI 异步：成员 NULL → NOT_SUPPORTED；有实现 → 透传
 */
static int stub_transfer_async(const struct bm_hal_spi *dev,
                               const uint8_t *tx, uint8_t *rx, size_t len,
                               bm_spi_transfer_done_fn_t done_cb, void *user) {
    (void)dev; (void)tx; (void)rx; (void)len; (void)done_cb; (void)user;
    return BM_OK;
}

void test_hal_spi_transfer_async_dispatch(void) {
    static const struct bm_spi_driver_api api_sync_only = { stub_transfer };
    static const struct bm_spi_driver_api api_async = {
        stub_transfer, stub_transfer_async,
    };
    bm_hal_spi_t dev_sync = { &api_sync_only, NULL };
    bm_hal_spi_t dev_async = { &api_async, NULL };
    bm_hal_spi_t noapi = { NULL, NULL };
    uint8_t      b = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_spi_transfer_async(&noapi, &b, &b, 1u, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED,
                      bm_hal_spi_transfer_async(&dev_sync, &b, &b, 1u, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_spi_transfer_async(&dev_async, NULL, NULL, 1u, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_spi_transfer_async(&dev_async, &b, &b, 0u, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_spi_transfer_async(&dev_async, &b, &b, 1u, NULL, NULL));
}

/**
 * @brief UART-dev：NULL/无 api 设备 → NOT_INIT / 0；参数边界
 */
void test_hal_uart_dev_dispatch_not_init(void) {
    bm_hal_uart_t dev;
    uint8_t           b = 0u;

    memset(&dev, 0, sizeof(dev));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_uart_init(NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_uart_init(&dev, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hal_uart_send(&dev, &b, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, bm_hal_uart_recv(&dev, &b, 1u));
    /* set_rx_callback 无后端静默返回，不崩溃 */
    bm_hal_uart_set_rx_callback(&dev, NULL);
    bm_hal_uart_set_rx_callback(NULL, NULL);
}

/**
 * @brief UART-dev：合法 api 桩下的参数校验（data=NULL 且 len>0 → INVALID）
 */
static int stub_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev; (void)config;
    return BM_OK;
}
static int stub_uart_send(const struct bm_hal_uart *dev,
                          const uint8_t *data, size_t len) {
    (void)dev; (void)data; (void)len;
    return BM_OK;
}
static size_t stub_uart_recv(const struct bm_hal_uart *dev,
                             uint8_t *data, size_t max_len) {
    (void)dev; (void)data; (void)max_len;
    return 1u;
}
static void stub_uart_rxcb(const struct bm_hal_uart *dev,
                           void (*cb)(uint8_t c)) {
    (void)dev; (void)cb;
}

void test_hal_uart_dev_dispatch_with_backend(void) {
    static const struct bm_uart_driver_api api = {
        stub_uart_init, stub_uart_send, stub_uart_recv, stub_uart_rxcb,
    };
    bm_hal_uart_t dev = { &api, NULL };
    uint8_t           b = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_init(&dev, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hal_uart_send(&dev, NULL, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_uart_send(&dev, &b, 1u));
    TEST_ASSERT_EQUAL_UINT(1u, bm_hal_uart_recv(&dev, &b, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, bm_hal_uart_recv(&dev, NULL, 1u));
    bm_hal_uart_set_rx_callback(&dev, NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hal_gpio_dispatch_not_init);
    RUN_TEST(test_hal_spi_dispatch_not_init_and_invalid);
    RUN_TEST(test_hal_spi_dispatch_invalid_args);
    RUN_TEST(test_hal_spi_transfer_async_dispatch);
    RUN_TEST(test_hal_uart_dev_dispatch_not_init);
    RUN_TEST(test_hal_uart_dev_dispatch_with_backend);
    return UNITY_END();
}

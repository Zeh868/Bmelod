/**
 * @file test_hal_i2c.c
 * @brief I2C 分发层边界测试（无后端 NOT_INIT、参数校验、fake api 参数透传）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（I2C 总线契约，接口批 2）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm_hal_i2c.h"
#include "bm/common/bm_types.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* fake api 调用现场记录（逐字段断言参数透传） */
static const struct bm_hal_i2c *g_last_dev;
static uint8_t                  g_last_addr;
static const uint8_t           *g_last_wbuf;
static uint8_t                 *g_last_rbuf;
static size_t                   g_last_wlen;
static size_t                   g_last_rlen;
static int                      g_ret;

static int stub_write(const struct bm_hal_i2c *dev, uint8_t addr,
                      const uint8_t *buf, size_t len) {
    g_last_dev  = dev;
    g_last_addr = addr;
    g_last_wbuf = buf;
    g_last_wlen = len;
    return g_ret;
}

static int stub_read(const struct bm_hal_i2c *dev, uint8_t addr,
                     uint8_t *buf, size_t len) {
    g_last_dev  = dev;
    g_last_addr = addr;
    g_last_rbuf = buf;
    g_last_rlen = len;
    return g_ret;
}

static int stub_write_read(const struct bm_hal_i2c *dev, uint8_t addr,
                           const uint8_t *wbuf, size_t wlen,
                           uint8_t *rbuf, size_t rlen) {
    g_last_dev  = dev;
    g_last_addr = addr;
    g_last_wbuf = wbuf;
    g_last_wlen = wlen;
    g_last_rbuf = rbuf;
    g_last_rlen = rlen;
    return g_ret;
}

static const struct bm_i2c_driver_api g_api = {
    stub_write, stub_read, stub_write_read,
};

/**
 * @brief NOT_INIT 三路径：NULL 设备 / 无 api / api 成员为 NULL
 */
void test_hal_i2c_dispatch_not_init(void) {
    static const struct bm_i2c_driver_api api_empty = { NULL, NULL, NULL };
    bm_hal_i2c_t dev_noapi   = { NULL, NULL };
    bm_hal_i2c_t dev_nomem   = { &api_empty, NULL };
    uint8_t      b           = 0u;

    /* NULL 设备 */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_write(NULL, 0x36u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_read(NULL, 0x36u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_write_read(NULL, 0x36u, &b, 1u, &b, 1u));
    /* 无 api */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_write(&dev_noapi, 0x36u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_read(&dev_noapi, 0x36u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_write_read(&dev_noapi, 0x36u, &b, 1u, &b, 1u));
    /* api 存在但成员为 NULL */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_write(&dev_nomem, 0x36u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_read(&dev_nomem, 0x36u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_hal_i2c_write_read(&dev_nomem, 0x36u, &b, 1u, &b, 1u));
}

/**
 * @brief INVALID 两路径：buf 为 NULL（len>0）与 len==0（经合法 api 桩）
 */
void test_hal_i2c_dispatch_invalid_args(void) {
    bm_hal_i2c_t dev = { &g_api, NULL };
    uint8_t      b   = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_write(&dev, 0x36u, NULL, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_write(&dev, 0x36u, &b, 0u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_read(&dev, 0x36u, NULL, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_read(&dev, 0x36u, &b, 0u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_write_read(&dev, 0x36u, NULL, 1u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_write_read(&dev, 0x36u, &b, 0u, &b, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_write_read(&dev, 0x36u, &b, 1u, NULL, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_hal_i2c_write_read(&dev, 0x36u, &b, 1u, &b, 0u));
}

/**
 * @brief OK 路径参数透传：dev/addr/buf/len 逐字段断言，返回值原样透传
 */
void test_hal_i2c_write_read_passthrough(void) {
    static const bm_i2c_config_t cfg = { 400000u, 10u };
    bm_hal_i2c_t dev = { &g_api, &cfg };
    uint8_t      wbuf[2] = { 0x0Eu, 0x0Fu };
    uint8_t      rbuf[4] = { 0u };

    g_ret = BM_OK;
    memset((void *)&g_last_dev, 0, sizeof(g_last_dev));

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_i2c_write(&dev, 0x36u, wbuf, 2u));
    TEST_ASSERT_EQUAL_PTR(&dev, g_last_dev);
    TEST_ASSERT_EQUAL_UINT8(0x36u, g_last_addr);
    TEST_ASSERT_EQUAL_PTR(wbuf, g_last_wbuf);
    TEST_ASSERT_EQUAL(2u, g_last_wlen);

    g_ret = BM_ERR_BUSY; /* 平台错误码原样透传 */
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_hal_i2c_read(&dev, 0x68u, rbuf, 4u));
    TEST_ASSERT_EQUAL_PTR(&dev, g_last_dev);
    TEST_ASSERT_EQUAL_UINT8(0x68u, g_last_addr);
    TEST_ASSERT_EQUAL_PTR(rbuf, g_last_rbuf);
    TEST_ASSERT_EQUAL(4u, g_last_rlen);

    g_ret = BM_OK;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_hal_i2c_write_read(&dev, 0x68u, wbuf, 2u, rbuf, 4u));
    TEST_ASSERT_EQUAL_PTR(&dev, g_last_dev);
    TEST_ASSERT_EQUAL_UINT8(0x68u, g_last_addr);
    TEST_ASSERT_EQUAL_PTR(wbuf, g_last_wbuf);
    TEST_ASSERT_EQUAL(2u, g_last_wlen);
    TEST_ASSERT_EQUAL_PTR(rbuf, g_last_rbuf);
    TEST_ASSERT_EQUAL(4u, g_last_rlen);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hal_i2c_dispatch_not_init);
    RUN_TEST(test_hal_i2c_dispatch_invalid_args);
    RUN_TEST(test_hal_i2c_write_read_passthrough);
    return UNITY_END();
}

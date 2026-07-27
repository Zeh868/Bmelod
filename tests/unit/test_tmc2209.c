/**
 * @file test_tmc2209.c
 * @brief tmc2209 组件单元测试（假 UART dev 验证帧格式/CRC/寄存器读写/堵转上报）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/tmc2209.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 假 UART dev（发送捕获 + 接收队列） ---------- */
static uint8_t g_tx_log[64];
static size_t  g_tx_len;
static uint8_t g_rx_fifo[128];
static size_t  g_rx_len;
static size_t  g_rx_pos;

static int fake_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev; (void)config;
    return BM_OK;
}
static int fake_uart_send(const struct bm_hal_uart *dev,
                          const uint8_t *data, size_t len) {
    (void)dev;
    TEST_ASSERT_TRUE(g_tx_len + len <= sizeof(g_tx_log));
    memcpy(g_tx_log + g_tx_len, data, len);
    g_tx_len += len;
    return BM_OK;
}
static size_t fake_uart_recv(const struct bm_hal_uart *dev,
                             uint8_t *data, size_t max_len) {
    size_t avail = g_rx_len - g_rx_pos;
    size_t n = (avail < max_len) ? avail : max_len;
    (void)dev;
    memcpy(data, g_rx_fifo + g_rx_pos, n);
    g_rx_pos += n;
    return n;
}
static void fake_uart_rxcb(const struct bm_hal_uart *dev,
                           void (*cb)(uint8_t c)) {
    (void)dev; (void)cb;
}

static const struct bm_uart_driver_api g_fake_uart_api = {
    fake_uart_init, fake_uart_send, fake_uart_recv, fake_uart_rxcb,
};
static const bm_hal_uart_t g_fake_uart = { &g_fake_uart_api, NULL };

/* ---------- 应答构造辅助（复用组件 CRC） ---------- */
static void queue_reply(uint8_t reg, uint32_t value) {
    uint8_t reply[8];

    reply[0] = 0x05u;
    reply[1] = 0xFFu;
    reply[2] = reg;
    reply[3] = (uint8_t)(value >> 24);
    reply[4] = (uint8_t)(value >> 16);
    reply[5] = (uint8_t)(value >> 8);
    reply[6] = (uint8_t)(value);
    reply[7] = bm_tmc2209_crc8(reply, 7u);
    TEST_ASSERT_TRUE(g_rx_len + 8u <= sizeof(g_rx_fifo));
    memcpy(g_rx_fifo + g_rx_len, reply, 8u);
    g_rx_len += 8u;
}

/** @brief 单线回环节字节（内容不校验，组件只丢弃） */
static void queue_echo(void) {
    TEST_ASSERT_TRUE(g_rx_len + 4u <= sizeof(g_rx_fifo));
    memset(g_rx_fifo + g_rx_len, 0xA5, 4u);
    g_rx_len += 4u;
}

static void make_axis(bm_tmc2209_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.uart        = &g_fake_uart;
    axis->config.slave_addr  = 2u;
    axis->config.single_wire = 1u;
    axis->config.rsense_ohm  = 0.11f;
    axis->resources.stall_threshold = 100u;
}

/** @brief 完成 init（IOIN 应答入队后调用 bm_tmc2209_init） */
static int init_axis(bm_tmc2209_axis_t *axis) {
    queue_echo();
    queue_reply(BM_TMC2209_REG_IOIN, 0x00000042u);
    return bm_tmc2209_init(axis);
}

static uint32_t g_stall_calls;
static uint16_t g_stall_sg;
static void stall_cb(void *user, uint16_t sg_result) {
    (void)user;
    g_stall_calls++;
    g_stall_sg = sg_result;
}

void setUp(void) {
    g_tx_len = 0u;
    g_rx_len = 0u;
    g_rx_pos = 0u;
    g_stall_calls = 0u;
    g_stall_sg = 0u;
    memset(g_tx_log, 0, sizeof(g_tx_log));
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

void test_tmc2209_validate_config(void) {
    bm_tmc2209_config_t cfg;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tmc2209_validate_config(NULL));
    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tmc2209_validate_config(&cfg));
    cfg.uart = &g_fake_uart;
    cfg.slave_addr = 4u;
    cfg.rsense_ohm = 0.11f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tmc2209_validate_config(&cfg));
    cfg.slave_addr = 2u;
    TEST_ASSERT_EQUAL(BM_OK, bm_tmc2209_validate_config(&cfg));
}

/**
 * @brief init：读 IOIN 验证通讯；读请求帧格式与 CRC 逐字节核对
 */
void test_tmc2209_init_ok(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, init_axis(&axis));
    TEST_ASSERT_EQUAL(1u, axis.state.comm_ok);

    /* 读请求帧：{0x05, slave=2, IOIN=0x06, crc} */
    TEST_ASSERT_EQUAL_UINT8(0x05u, g_tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, g_tx_log[1]);
    TEST_ASSERT_EQUAL_UINT8(BM_TMC2209_REG_IOIN, g_tx_log[2]);
    TEST_ASSERT_EQUAL_UINT8(bm_tmc2209_crc8(g_tx_log, 3u), g_tx_log[3]);
}

void test_tmc2209_init_timeout(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    /* 空接收队列 → 回环节字节都收不到 → TIMEOUT */
    TEST_ASSERT_EQUAL(BM_ERR_TIMEOUT, bm_tmc2209_init(&axis));
    TEST_ASSERT_EQUAL(0u, axis.state.comm_ok);
}

void test_tmc2209_init_bad_crc(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    queue_echo();
    queue_reply(BM_TMC2209_REG_IOIN, 0x42u);
    g_rx_fifo[g_rx_len - 1u] ^= 0xFFu; /* 破坏 CRC */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tmc2209_init(&axis));
    TEST_ASSERT_EQUAL(0u, axis.state.comm_ok);
}

/**
 * @brief write_reg：8 字节写帧逐字段核对（含 WRITE 位与 CRC）
 */
void test_tmc2209_write_reg_frame(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, init_axis(&axis));

    g_tx_len = 0u;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_tmc2209_write_reg(&axis, BM_TMC2209_REG_IHOLD_IRUN,
                                           0x00030405u));
    TEST_ASSERT_EQUAL_UINT(8u, g_tx_len);
    TEST_ASSERT_EQUAL_UINT8(0x05u, g_tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02u, g_tx_log[1]);
    TEST_ASSERT_EQUAL_UINT8(BM_TMC2209_REG_IHOLD_IRUN | 0x80u, g_tx_log[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[3]);
    TEST_ASSERT_EQUAL_UINT8(0x03u, g_tx_log[4]);
    TEST_ASSERT_EQUAL_UINT8(0x04u, g_tx_log[5]);
    TEST_ASSERT_EQUAL_UINT8(0x05u, g_tx_log[6]);
    TEST_ASSERT_EQUAL_UINT8(bm_tmc2209_crc8(g_tx_log, 7u), g_tx_log[7]);
}

/**
 * @brief read_reg：应答解析（值重组）与未初始化 NOT_INIT
 */
void test_tmc2209_read_reg_value(void) {
    bm_tmc2209_axis_t axis;
    uint32_t value = 0u;

    make_axis(&axis);
    /* 未 init → NOT_INIT */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT,
                      bm_tmc2209_read_reg(&axis, BM_TMC2209_REG_GSTAT, &value));

    TEST_ASSERT_EQUAL(BM_OK, init_axis(&axis));
    queue_echo();
    queue_reply(BM_TMC2209_REG_GSTAT, 0xDEADBEEFu);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_tmc2209_read_reg(&axis, BM_TMC2209_REG_GSTAT, &value));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, value);
}

/**
 * @brief set_current：IHOLD_IRUN 值组装（ihold|irun<<8|iholddelay<<16）
 */
void test_tmc2209_set_current(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, init_axis(&axis));

    g_tx_len = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_tmc2209_set_current(&axis, 5u, 16u, 3u));
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[3]);
    TEST_ASSERT_EQUAL_UINT8(0x03u, g_tx_log[4]); /* iholddelay<<16 */
    TEST_ASSERT_EQUAL_UINT8(16u,   g_tx_log[5]); /* irun<<8 */
    TEST_ASSERT_EQUAL_UINT8(5u,    g_tx_log[6]); /* ihold */
    /* 越界参数 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_tmc2209_set_current(&axis, 32u, 16u, 3u));
}

/**
 * @brief set_microsteps：读-改-写 CHOPCONF，MRES 域 bits27:24
 */
void test_tmc2209_set_microsteps(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, init_axis(&axis));

    /* 读 CHOPCONF 应答（原 MRES=4） */
    queue_echo();
    queue_reply(BM_TMC2209_REG_CHOPCONF, 0x14000000u);
    g_tx_len = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_tmc2209_set_microsteps(&axis, 2u));
    /* tx 布局：4B 读请求 + 8B 写帧；写帧从 g_tx_log[4] 起，
     * MRES 域被替换为 2（0x12<<24），其余位保持 */
    TEST_ASSERT_EQUAL_UINT8(0x12u, g_tx_log[7]);  /* 写帧 byte3 = value>>24 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[8]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[9]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[10]);
    /* 越界 mres */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tmc2209_set_microsteps(&axis, 9u));
}

/**
 * @brief poll：SG_RESULT 跌破阈值沿触发一次上报，恢复后滞回解除
 */
void test_tmc2209_poll_stall_edge(void) {
    bm_tmc2209_axis_t axis;
    make_axis(&axis);
    axis.resources.stall_callback = stall_cb;
    TEST_ASSERT_EQUAL(BM_OK, init_axis(&axis));

    /* sg=50 < 100 → 触发一次 */
    queue_echo();
    queue_reply(BM_TMC2209_REG_SG_RESULT, 50u);
    bm_tmc2209_poll(&axis);
    TEST_ASSERT_EQUAL_UINT32(1u, g_stall_calls);
    TEST_ASSERT_EQUAL_UINT16(50u, g_stall_sg);
    TEST_ASSERT_EQUAL(1, axis.state.stalled);

    /* 仍 50 → 不重复触发 */
    queue_echo();
    queue_reply(BM_TMC2209_REG_SG_RESULT, 50u);
    bm_tmc2209_poll(&axis);
    TEST_ASSERT_EQUAL_UINT32(1u, g_stall_calls);

    /* sg=200 ≥ 100 → 滞回解除 */
    queue_echo();
    queue_reply(BM_TMC2209_REG_SG_RESULT, 200u);
    bm_tmc2209_poll(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.stalled);
    TEST_ASSERT_EQUAL_UINT32(1u, g_stall_calls);
}

/**
 * @brief NULL 安全与 CRC 自检（datasheet 性质：空帧 CRC=0）
 */
void test_tmc2209_null_safety_and_crc(void) {
    TEST_ASSERT_EQUAL_UINT8(0u, bm_tmc2209_crc8(NULL, 0u));
    bm_tmc2209_poll(NULL);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tmc2209_init(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tmc2209_validate_config);
    RUN_TEST(test_tmc2209_init_ok);
    RUN_TEST(test_tmc2209_init_timeout);
    RUN_TEST(test_tmc2209_init_bad_crc);
    RUN_TEST(test_tmc2209_write_reg_frame);
    RUN_TEST(test_tmc2209_read_reg_value);
    RUN_TEST(test_tmc2209_set_current);
    RUN_TEST(test_tmc2209_set_microsteps);
    RUN_TEST(test_tmc2209_poll_stall_edge);
    RUN_TEST(test_tmc2209_null_safety_and_crc);
    return UNITY_END();
}

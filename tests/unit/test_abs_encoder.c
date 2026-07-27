/**
 * @file test_abs_encoder.c
 * @brief abs_encoder 组件单元测试（假 SPI 验证 AS5047P 帧格式/偶校验/错误位）
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
#include "bm/component/abs_encoder.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 假 SPI（命令捕获 + 应答队列，每 transfer 消耗一帧） ---------- */
static uint8_t  g_tx_log[16];
static size_t   g_tx_len;
static uint16_t g_resp_queue[8];
static size_t   g_resp_len;
static size_t   g_resp_pos;

static int fake_spi_transfer(const struct bm_hal_spi *dev,
                             const uint8_t *tx, uint8_t *rx, size_t len) {
    uint16_t resp;
    (void)dev;
    TEST_ASSERT_EQUAL_UINT(2u, len);
    TEST_ASSERT_TRUE(g_tx_len + 2u <= sizeof(g_tx_log));
    g_tx_log[g_tx_len++] = tx[0];
    g_tx_log[g_tx_len++] = tx[1];
    TEST_ASSERT_TRUE(g_resp_pos < g_resp_len);
    resp = g_resp_queue[g_resp_pos++];
    rx[0] = (uint8_t)(resp >> 8);
    rx[1] = (uint8_t)(resp);
    return BM_OK;
}

static const struct bm_spi_driver_api g_fake_spi_api = { fake_spi_transfer };

/** @brief 测试侧偶校验（与 AS5047P 规则一致：对 bit14:0 取偶） */
static uint16_t test_parity(uint16_t frame) {
    uint16_t v = frame & 0x7FFFu;
    uint16_t p = 0u;
    while (v != 0u) {
        p ^= (uint16_t)(v & 1u);
        v >>= 1;
    }
    return p;
}

/** @brief 组一帧合法应答（数据 + 校验位），可附加错误位。 */
static uint16_t make_resp(uint16_t data14, int err) {
    uint16_t resp = (data14 & 0x3FFFu) | (err ? (1u << 14) : 0u);
    if (test_parity(resp) != 0u) {
        resp |= (1u << 15);
    }
    return resp;
}

static bm_abs_encoder_as5047p_config_t g_cfg;
static bm_hal_spi_t                    g_spi;
static bm_hal_abs_encoder_t            g_enc;

void setUp(void) {
    g_tx_len = 0u;
    g_resp_len = 0u;
    g_resp_pos = 0u;
    g_spi.api  = &g_fake_spi_api;
    g_spi.config = NULL;
    g_cfg.spi  = &g_spi;
    g_enc.api  = &bm_abs_encoder_as5047p_api;
    g_enc.config = &g_cfg;
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

/**
 * @brief read_angle：流水两帧——读命令 0xFFFF（0x3FFF|读位|校验），
 *        NOP 0x0000；应答数据 14bit 原样取回
 */
void test_abs_encoder_read_angle_frames(void) {
    uint16_t raw = 0u;

    g_resp_queue[g_resp_len++] = make_resp(0u, 0);          /* 首帧应答（丢弃） */
    g_resp_queue[g_resp_len++] = make_resp(0x1234u, 0);     /* 角度帧 */
    TEST_ASSERT_EQUAL(BM_OK, bm_abs_encoder_read_angle(&g_enc, &raw));
    TEST_ASSERT_EQUAL_UINT16(0x1234u, raw);

    /* 命令帧核对：read(0x3FFF) → 0xFFFF；NOP → 0x0000 */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, g_tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, g_tx_log[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_tx_log[3]);
}

/**
 * @brief read_status：错误位 + MAGH 映射（bit15=ERR，bit1=MAGH，bit0=MAGL）
 */
void test_abs_encoder_read_status_mapping(void) {
    uint16_t status = 0u;

    g_resp_queue[g_resp_len++] = make_resp(0u, 0);
    /* DIAAGC：MAGH=bit9，附帧错误位 */
    g_resp_queue[g_resp_len++] = make_resp((1u << 9), 1);
    TEST_ASSERT_EQUAL(BM_OK, bm_abs_encoder_read_status(&g_enc, &status));
    TEST_ASSERT_EQUAL_UINT16((1u << 15) | (1u << 1), status);

    /* 命令帧：read(0x3FFC) → 0x7FFC 共 13 个 1 位（奇）→ 校验位置位 → 0xFFFC */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, g_tx_log[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFCu, g_tx_log[1]);
}

/**
 * @brief MAGL 映射（bit10 → status bit0）
 */
void test_abs_encoder_status_magl(void) {
    uint16_t status = 0u;

    g_resp_queue[g_resp_len++] = make_resp(0u, 0);
    g_resp_queue[g_resp_len++] = make_resp((1u << 10), 0);
    TEST_ASSERT_EQUAL(BM_OK, bm_abs_encoder_read_status(&g_enc, &status));
    TEST_ASSERT_EQUAL_UINT16(1u, status);
}

/**
 * @brief 应答偶校验错 → BM_ERR_INVALID
 */
void test_abs_encoder_parity_error(void) {
    uint16_t raw = 0u;

    g_resp_queue[g_resp_len++] = make_resp(0u, 0);
    g_resp_queue[g_resp_len++] = 0x0001u; /* 数据 1 个 1 位但校验位 0 → 错 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_abs_encoder_read_angle(&g_enc, &raw));
}

/**
 * @brief 分发边界：无 api → NOT_INIT；NULL 参数 → INVALID
 */
void test_abs_encoder_dispatch_bounds(void) {
    bm_hal_abs_encoder_t noapi = { NULL, NULL };
    uint16_t v = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_abs_encoder_read_angle(NULL, &v));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_abs_encoder_read_angle(&noapi, &v));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_abs_encoder_read_status(&noapi, &v));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_abs_encoder_read_angle(&g_enc, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_abs_encoder_read_status(&g_enc, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_abs_encoder_read_angle_frames);
    RUN_TEST(test_abs_encoder_read_status_mapping);
    RUN_TEST(test_abs_encoder_status_magl);
    RUN_TEST(test_abs_encoder_parity_error);
    RUN_TEST(test_abs_encoder_dispatch_bounds);
    return UNITY_END();
}

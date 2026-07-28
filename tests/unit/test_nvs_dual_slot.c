/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_nvs_dual_slot.c
 * @brief NVS 双槽格式单元测试（损坏槽 / 半写 / 序号择优）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增双槽 NVS 格式单测
 */
#include "unity.h"
#include "bm_nvs_dual_slot.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <string.h>

#define TEST_SLOT_SIZE  256u
#define TEST_PAYLOAD_LEN 32u

static uint8_t s_slot_a[TEST_SLOT_SIZE];
static uint8_t s_slot_b[TEST_SLOT_SIZE];
static uint8_t s_payload[TEST_PAYLOAD_LEN];

void setUp(void) {
    (void)memset(s_slot_a, 0xFF, sizeof(s_slot_a));
    (void)memset(s_slot_b, 0xFF, sizeof(s_slot_b));
    (void)memset(s_payload, 0xA5, sizeof(s_payload));
}

void tearDown(void) {
}

static void test_nvs_slot_min_size(void) {
    TEST_ASSERT_EQUAL(BM_NVS_SLOT_OVERHEAD + 32u,
                      bm_nvs_slot_min_size(32u));
    TEST_ASSERT_TRUE(bm_nvs_slot_min_size(TEST_PAYLOAD_LEN) <= TEST_SLOT_SIZE);
}

static void test_nvs_empty_slots_not_found(void) {
    int which = -1;
    uint32_t seq = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
                      bm_nvs_dual_pick_active(s_slot_a, s_slot_b, TEST_SLOT_SIZE,
                                              TEST_PAYLOAD_LEN, &which, &seq));
}

static void test_nvs_pack_parse_roundtrip(void) {
    uint32_t seq = 0u;
    const uint8_t *payload = NULL;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_a, TEST_SLOT_SIZE, 7u,
                                       s_payload, TEST_PAYLOAD_LEN));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_parse(s_slot_a, TEST_SLOT_SIZE,
                                        TEST_PAYLOAD_LEN, &seq, &payload));
    TEST_ASSERT_EQUAL(7u, seq);
    TEST_ASSERT_EQUAL_MEMORY(s_payload, payload, TEST_PAYLOAD_LEN);
}

static void test_nvs_corrupt_crc_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_a, TEST_SLOT_SIZE, 1u,
                                       s_payload, TEST_PAYLOAD_LEN));
    /* 破坏载荷一字节 → CRC 失配 */
    s_slot_a[BM_NVS_SLOT_HDR_SIZE] ^= 0x01u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_nvs_slot_parse(s_slot_a, TEST_SLOT_SIZE,
                                        TEST_PAYLOAD_LEN, NULL, NULL));
}

static void test_nvs_half_write_prefers_intact(void) {
    int which = -1;
    uint32_t seq = 0u;
    uint8_t payload2[TEST_PAYLOAD_LEN];

    (void)memset(payload2, 0x5A, sizeof(payload2));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_a, TEST_SLOT_SIZE, 3u,
                                       s_payload, TEST_PAYLOAD_LEN));
    /* 模拟 B 槽断电半写：写了头但 CRC 损坏 */
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_b, TEST_SLOT_SIZE, 4u,
                                       payload2, TEST_PAYLOAD_LEN));
    s_slot_b[BM_NVS_SLOT_HDR_SIZE + TEST_PAYLOAD_LEN] ^= 0xFFu;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_dual_pick_active(s_slot_a, s_slot_b, TEST_SLOT_SIZE,
                                              TEST_PAYLOAD_LEN, &which, &seq));
    TEST_ASSERT_EQUAL(0, which);
    TEST_ASSERT_EQUAL(3u, seq);
}

static void test_nvs_newer_seq_wins(void) {
    int which = -1;
    uint32_t seq = 0u;
    uint8_t payload2[TEST_PAYLOAD_LEN];

    (void)memset(payload2, 0x11, sizeof(payload2));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_a, TEST_SLOT_SIZE, 10u,
                                       s_payload, TEST_PAYLOAD_LEN));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_b, TEST_SLOT_SIZE, 11u,
                                       payload2, TEST_PAYLOAD_LEN));

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_dual_pick_active(s_slot_a, s_slot_b, TEST_SLOT_SIZE,
                                              TEST_PAYLOAD_LEN, &which, &seq));
    TEST_ASSERT_EQUAL(1, which);
    TEST_ASSERT_EQUAL(11u, seq);
}

static void test_nvs_wrong_payload_len_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_nvs_slot_pack(s_slot_a, TEST_SLOT_SIZE, 1u,
                                       s_payload, TEST_PAYLOAD_LEN));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_nvs_slot_parse(s_slot_a, TEST_SLOT_SIZE,
                                        (uint16_t)(TEST_PAYLOAD_LEN + 1u),
                                        NULL, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nvs_slot_min_size);
    RUN_TEST(test_nvs_empty_slots_not_found);
    RUN_TEST(test_nvs_pack_parse_roundtrip);
    RUN_TEST(test_nvs_corrupt_crc_rejected);
    RUN_TEST(test_nvs_half_write_prefers_intact);
    RUN_TEST(test_nvs_newer_seq_wins);
    RUN_TEST(test_nvs_wrong_payload_len_rejected);
    return UNITY_END();
}

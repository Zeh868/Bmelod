/**
 * @file test_stream_frontend.c
 * @brief stream_frontend 组件单元测试
 *
 * 覆盖块流提交/消费与时钟漂移遥测基本行为。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 */
#include "unity.h"
#include "bm/component/stream_frontend.h"
#include "bm/common/bm_types.h"

#include <string.h>

typedef struct {
    uint32_t marker;
} test_payload_t;

static test_payload_t s_payloads[2];
static bm_block_t s_blocks[2];
static bm_stream_t s_stream;

void setUp(void) {
    memset(&s_stream, 0, sizeof(s_stream));
    s_stream.blocks = s_blocks;
    s_stream.block_count = 2u;
    s_stream.block_capacity = 2u;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_init(&s_stream, s_payloads, 2u,
                                     sizeof(test_payload_t)));
    bm_stream_reset(&s_stream);
}

void tearDown(void) {}

void test_stream_frontend_submit_consume_roundtrip(void) {
    bm_stream_frontend_axis_t axis;
    bm_block_t *prod;
    bm_block_t *cons;
    bm_timestamp_t ts = {
        .rate_hz = 1000000u,
        .ticks = 10000u
    };
    test_payload_t *payload;

    memset(&axis, 0, sizeof(axis));
    axis.config.expected_block_period_us = 10000u;
    axis.config.drift_alpha = 0.5f;
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_frontend_init(&axis, &s_stream));

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_frontend_producer_acquire(&axis, &prod));
    payload = (test_payload_t *)prod->data;
    payload->marker = 0xBEEFu;
    ts.ticks = 10000u;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_frontend_on_block_submit(&axis, prod,
                                                        sizeof(test_payload_t),
                                                        &ts));

    ts.ticks = 20000u;
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_frontend_producer_acquire(&axis, &prod));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_stream_frontend_on_block_submit(&axis, prod,
                                                        sizeof(test_payload_t),
                                                        &ts));

    TEST_ASSERT_EQUAL(BM_OK, bm_stream_frontend_on_block_consume(&axis, &cons));
    TEST_ASSERT_EQUAL(0xBEEFu, ((test_payload_t *)cons->data)->marker);
    TEST_ASSERT_EQUAL(BM_OK, bm_stream_frontend_block_release(&axis, cons));

    TEST_ASSERT_EQUAL(1u, axis.state.telemetry.blocks_consumed);
    TEST_ASSERT_TRUE(axis.state.telemetry.drift_ratio > 0.0f);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stream_frontend_submit_consume_roundtrip);
    return UNITY_END();
}

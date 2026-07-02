/**
 * @file test_transport_qos_token.c
 * @brief transport_qos token bucket 单元测试
 *
 * 覆盖 token bucket 入队接受与超额丢弃。
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
#include "bm/component/transport_qos.h"
#include "bm/common/bm_types.h"

#include <string.h>

static uint32_t g_now_ms;

static uint32_t mock_now_ms(void *user) {
    (void)user;
    return g_now_ms;
}

void setUp(void) {
    g_now_ms = 0u;
}
void tearDown(void) {}

void test_transport_qos_enqueue_token_bucket(void) {
    bm_transport_qos_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.ema_alpha = 0.1f;
    axis.config.token_rate_bytes_per_ms = 1.0f;
    axis.config.token_burst_bytes = 100u;

    TEST_ASSERT_EQUAL(BM_OK, bm_transport_qos_init(&axis));
    TEST_ASSERT_EQUAL(0, bm_transport_qos_enqueue(&axis, 50u));
    TEST_ASSERT_EQUAL(0, bm_transport_qos_enqueue(&axis, 50u));
    TEST_ASSERT_EQUAL(-1, bm_transport_qos_enqueue(&axis, 1u));
}

/*
 * P0-5c：令牌桶按经过时间补充。桶烧空后推进时间，令牌应按速率恢复、
 * 使后续 enqueue 重新被接受；补充上限不超过 burst 容量。
 */
void test_transport_qos_token_refill_over_time(void) {
    bm_transport_qos_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.ema_alpha = 0.1f;
    axis.config.token_rate_bytes_per_ms = 1.0f; /* 1 字节/ms */
    axis.config.token_burst_bytes = 100u;
    axis.resources.now_ms = mock_now_ms;

    g_now_ms = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_transport_qos_init(&axis));

    /* 烧空初始 100 令牌（首次 enqueue 登记时间基准，不补充） */
    TEST_ASSERT_EQUAL(0, bm_transport_qos_enqueue(&axis, 100u));
    /* 同一时刻再入队：无补充，桶空拒绝 */
    TEST_ASSERT_EQUAL(-1, bm_transport_qos_enqueue(&axis, 1u));

    /* 推进 50ms：补充 50 令牌，恰好接受 50 字节 */
    g_now_ms = 50u;
    TEST_ASSERT_EQUAL(0, bm_transport_qos_enqueue(&axis, 50u));
    /* 桶再次空 */
    TEST_ASSERT_EQUAL(-1, bm_transport_qos_enqueue(&axis, 1u));

    /* 推进很久：补充被 burst(100) 封顶，可接受但不超过 100 */
    g_now_ms = 100000u;
    TEST_ASSERT_EQUAL(0, bm_transport_qos_enqueue(&axis, 100u));
    TEST_ASSERT_EQUAL(-1, bm_transport_qos_enqueue(&axis, 1u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_qos_enqueue_token_bucket);
    RUN_TEST(test_transport_qos_token_refill_over_time);
    return UNITY_END();
}

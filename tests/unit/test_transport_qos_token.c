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

void setUp(void) {}
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_qos_enqueue_token_bucket);
    return UNITY_END();
}

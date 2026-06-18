/**
 * @file test_ultra.c
 * @brief Ultra 轻量事件发布、处理与队列溢出单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_ultra.h"
#include "bm_log.h"

static int g_count = 0;
static uintptr_t g_data_address = 0u;

#define EVENT_TEST 1

static void test_cb(const void *data, uint8_t len) {
    g_count++;
    g_data_address = (uintptr_t)data;
    if (data && len == sizeof(uint16_t)) {
        uint16_t val = *(const uint16_t *)data;
        TEST_ASSERT_EQUAL(42, val);
    }
}

BM_ULTRA_CALLBACK_TABLE_DEFINE(
    BM_ULTRA_CB(EVENT_TEST, test_cb)
);

void setUp(void) {
    BM_LOGI("test_ultra", "setUp: reset ultra subsystem");
    g_count = 0;
    g_data_address = 0u;
    bm_ultra_init();
}
void tearDown(void) {}

void test_ultra_publish_and_process(void) {
    uint16_t data = 42;
    TEST_ASSERT_EQUAL(BM_OK, bm_ultra_publish(EVENT_TEST, &data, sizeof(data)));
    TEST_ASSERT_EQUAL(1, bm_ultra_event_count());
    TEST_ASSERT_EQUAL(1, bm_ultra_process());
    TEST_ASSERT_EQUAL(1, g_count);
    TEST_ASSERT_EQUAL(0u, g_data_address % 8u);
    TEST_ASSERT_EQUAL(0, bm_ultra_event_count());
}

void test_ultra_rejects_invalid_event_type(void) {
    uint8_t data = 1u;
    bm_ultra_queue_item_t bad = { .event_type = 255u, .data_len = 1u };

    bad.data[0] = data;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ultra_publish((bm_event_type_t)255, &data, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ultra_queue_push(&bad));
    TEST_ASSERT_EQUAL(0u, bm_ultra_event_count());
}

void test_ultra_rejects_null_payload(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_ultra_publish(EVENT_TEST, NULL, 1u));
    TEST_ASSERT_EQUAL(0u, bm_ultra_event_count());
}

void test_ultra_dispatch_skipped_invalid_type(void) {
    bm_ultra_queue_item_t bad = { .event_type = 255u, .data_len = 0u };

    TEST_ASSERT_EQUAL(0u, bm_ultra_get_dispatch_skipped_count());
    TEST_ASSERT_EQUAL(BM_OK, bm_ultra_test_inject(&bad));
    TEST_ASSERT_EQUAL(1u, bm_ultra_process());
    TEST_ASSERT_EQUAL(1u, bm_ultra_get_dispatch_skipped_count());
}

void test_ultra_dispatch_skips_invalid_payload_length(void) {
    bm_ultra_queue_item_t bad = {
        .event_type = EVENT_TEST,
        .data_len = (uint8_t)(BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE + 1u)
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_ultra_test_inject(&bad));
    TEST_ASSERT_EQUAL(1u, bm_ultra_process());
    TEST_ASSERT_EQUAL(0, g_count);
    TEST_ASSERT_EQUAL(1u, bm_ultra_get_dispatch_skipped_count());
}

void test_ultra_dispatch_skipped_corrupt_queue(void) {
    bm_ultra_queue_t *q = (bm_ultra_queue_t *)bm_ultra_queue_state();

    TEST_ASSERT_EQUAL(0u, bm_ultra_get_dispatch_skipped_count());
    q->read_idx = (uint8_t)(BM_CONFIG_ULTRA_QUEUE_DEPTH + 1u);
    TEST_ASSERT_EQUAL(0u, bm_ultra_process());
    TEST_ASSERT_EQUAL(1u, bm_ultra_get_dispatch_skipped_count());
}

void test_ultra_queue_overflow(void) {
    uint32_t dropped_before = bm_ultra_get_dropped_count();

    for (int i = 0; i < BM_CONFIG_ULTRA_QUEUE_DEPTH + 2; i++) {
        uint8_t dummy = (uint8_t)i;
        bm_ultra_publish(EVENT_TEST, &dummy, sizeof(dummy));
    }
    TEST_ASSERT_GREATER_THAN_UINT32(dropped_before,
                                  bm_ultra_get_dropped_count());
    uint8_t processed = 0;
    while (bm_ultra_process()) {
        processed++;
    }
    /* 队列深度 8，保留 1 槽区分满/空，故最多可存 7 条 */
    TEST_ASSERT_EQUAL(BM_CONFIG_ULTRA_QUEUE_DEPTH - 1, processed);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ultra_publish_and_process);
    RUN_TEST(test_ultra_rejects_invalid_event_type);
    RUN_TEST(test_ultra_rejects_null_payload);
    RUN_TEST(test_ultra_dispatch_skipped_invalid_type);
    RUN_TEST(test_ultra_dispatch_skips_invalid_payload_length);
    RUN_TEST(test_ultra_dispatch_skipped_corrupt_queue);
    RUN_TEST(test_ultra_queue_overflow);
    return UNITY_END();
}

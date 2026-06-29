/**
 * @file test_ticker.c
 * @brief 软定时 Ticker 周期发布、队列溢出与 64 位 µs 时间基单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-26
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-26       1.1            zeh            #9-2b 迁 bm_uptime_us；
 *                                                测试辅助从 advance_ticks 改为
 *                                                advance_us；移除 32 位回绕场景
 */

#include "unity.h"
#include "bm_ticker.h"
#include "bm_hal_timer.h"
#include "bm_hal_timer_native.h"
#include "bm_hal_uptime_native.h"
#include "bm_log.h"

#define TICKER_EVT 3u

static int test_event_process_frozen(uint32_t budget) {
    bm_event_freeze_subscriptions();
    return bm_event_process(budget);
}

#define bm_event_process test_event_process_frozen

static int g_event_count;

static void ticker_cb(const bm_event_t *ev, void *user_data) {
    (void)user_data;
    if (ev->type == TICKER_EVT) {
        g_event_count++;
    }
}

void setUp(void) {
    BM_LOGI("test_ticker", "setUp: reset event and ticker");
    g_event_count = 0;
    bm_event_reset();
    bm_hal_uptime_native_reset();
    bm_hal_timer_native_reset_ticks();
    bm_ticker_reset();
    bm_hal_timer_init(1000u);
}

void tearDown(void) {
    bm_ticker_reset();
}

void test_ticker_publishes_on_period(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };
    bm_event_subscriber_id_t id;

    bm_event_register_type(TICKER_EVT, "TICK");
    bm_event_subscribe(TICKER_EVT, ticker_cb, NULL, &id);
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));

    bm_hal_uptime_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
    TEST_ASSERT_EQUAL(1, bm_event_process(4));
    TEST_ASSERT_EQUAL(1, g_event_count);

    bm_hal_uptime_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
    TEST_ASSERT_EQUAL(1, bm_event_process(4));
    TEST_ASSERT_EQUAL(2, g_event_count);

    bm_event_unsubscribe(TICKER_EVT, id);
}

void test_ticker_counts_dropped_when_queue_full(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };
    uint32_t i;

    bm_event_register_type(TICKER_EVT, "TICK");
    i = 0u;
    while (bm_event_publish_copy(TICKER_EVT, 1u, NULL, 0u) == BM_OK) {
        i++;
    }
    TEST_ASSERT_GREATER_THAN(0u, i);
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
                      bm_event_publish_copy(TICKER_EVT, 1u, NULL, 0u));

    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));
    bm_hal_uptime_native_advance_us(20000u);
    (void)bm_ticker_poll();
    BM_LOGE("test_ticker", "expect dropped count when event queue full");
    TEST_ASSERT_GREATER_THAN(0u, bm_ticker_get_dropped(0u));
}

void test_ticker_rejects_reinit(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };

    bm_event_register_type(TICKER_EVT, "TICK");
    TEST_ASSERT_EQUAL(0, bm_ticker_is_initialized());
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));
    TEST_ASSERT_EQUAL(1, bm_ticker_is_initialized());
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_ticker_init(slots, 1u));
    bm_ticker_reset();
    TEST_ASSERT_EQUAL(0, bm_ticker_is_initialized());
}

void test_ticker_rejects_invalid_event_type(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, (bm_event_type_t)BM_CONFIG_MAX_EVENT_TYPES, 1u, "bad" },
    };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_ticker_init(slots, 1u));
}

/**
 * @brief 迁到 bm_uptime_us 后 ticker 不再依赖 HAL timer；
 *        HAL timer deinit 后 init 应仍然成功
 */
void test_ticker_init_succeeds_without_hal_timer(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };

    bm_event_register_type(TICKER_EVT, "TICK");
    bm_hal_timer_native_deinit();
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));
    TEST_ASSERT_EQUAL(1, bm_ticker_is_initialized());
    /* 恢复，避免影响后续 setUp */
    bm_hal_timer_init(1000u);
}

/**
 * @brief 将 uptime 偏移推进到接近旧 32 位最大值（µs 域），
 *        验证 64 位算术在大时间戳下仍正确（替代原 32 位回绕测试）
 */
void test_ticker_large_uptime_offset(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };
    bm_event_subscriber_id_t id;

    bm_event_register_type(TICKER_EVT, "TICK");
    bm_event_subscribe(TICKER_EVT, ticker_cb, NULL, &id);

    /* 推进到接近旧 32 位溢出点（µs 域），不影响 64 位单调时钟 */
    bm_hal_uptime_native_advance_us((uint64_t)0xFFFFFFFBu);
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));
    bm_hal_uptime_native_advance_us(9000u);
    TEST_ASSERT_EQUAL(0, bm_ticker_poll());
    bm_hal_uptime_native_advance_us(1000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
    TEST_ASSERT_EQUAL(1, bm_event_process(4));
    TEST_ASSERT_EQUAL(1, g_event_count);
    bm_hal_uptime_native_advance_us(9000u);
    TEST_ASSERT_EQUAL(0, bm_ticker_poll());
    bm_hal_uptime_native_advance_us(1000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
    TEST_ASSERT_EQUAL(1, bm_event_process(4));
    TEST_ASSERT_EQUAL(2, g_event_count);

    bm_event_unsubscribe(TICKER_EVT, id);
}

void test_ticker_catches_up_multiple_periods_without_drift(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };

    bm_event_register_type(TICKER_EVT, "TICK");
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));

    bm_hal_uptime_native_advance_us(35000u);
    TEST_ASSERT_EQUAL(3, bm_ticker_poll());
    TEST_ASSERT_EQUAL(0, bm_ticker_poll());
    TEST_ASSERT_EQUAL(3, bm_event_process(4));

    bm_hal_uptime_native_advance_us(5000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
}

void test_ticker_propagates_non_overflow_publish_error(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };

    bm_event_register_type(TICKER_EVT, "TICK");
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));
    bm_event_reset();
    bm_hal_uptime_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_ticker_poll());
    TEST_ASSERT_EQUAL(0u, bm_ticker_get_dropped(0u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ticker_publishes_on_period);
    RUN_TEST(test_ticker_counts_dropped_when_queue_full);
    RUN_TEST(test_ticker_rejects_reinit);
    RUN_TEST(test_ticker_rejects_invalid_event_type);
    RUN_TEST(test_ticker_init_succeeds_without_hal_timer);
    RUN_TEST(test_ticker_large_uptime_offset);
    RUN_TEST(test_ticker_catches_up_multiple_periods_without_drift);
    RUN_TEST(test_ticker_propagates_non_overflow_publish_error);
    return UNITY_END();
}

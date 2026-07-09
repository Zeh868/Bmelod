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
#define TICKER_EVT2 4u

static int test_event_process_frozen(uint32_t budget) {
    bm_event_freeze_subscriptions();
    return bm_event_process(budget);
}

#define bm_event_process test_event_process_frozen

static int g_event_count;
static int g_event_count2;

static void ticker_cb(const bm_event_t *ev, void *user_data) {
    (void)user_data;
    if (ev->type == TICKER_EVT) {
        g_event_count++;
    }
}

static void ticker_cb2(const bm_event_t *ev, void *user_data) {
    (void)user_data;
    if (ev->type == TICKER_EVT2) {
        g_event_count2++;
    }
}

void setUp(void) {
    BM_LOGI("test_ticker", "setUp: reset event and ticker");
    g_event_count = 0;
    g_event_count2 = 0;
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

/**
 * @brief H1 回归：非 OVERFLOW 发布失败（如事件类型未注册）不得让 poll
 *        提前 return 中断整轮槽循环——本槽应按丢弃计数处理并推进 next_us，
 *        poll 正常返回已发布计数（此处为 0），而非把错误码向上传播。
 */
void test_ticker_non_overflow_publish_error_counts_as_dropped(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };

    bm_event_register_type(TICKER_EVT, "TICK");
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));
    /* bm_event_reset 撤销事件类型注册，使该槽的 publish_copy 返回
     * BM_ERR_NOT_INIT（非 OVERFLOW）。 */
    bm_event_reset();
    bm_hal_uptime_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(0, bm_ticker_poll());
    TEST_ASSERT_GREATER_THAN(0u, bm_ticker_get_dropped(0u));
}

/**
 * @brief H1 回归：一个槽发布失败不应阻塞同一轮 poll 中的其它槽——
 *        槽 0 的事件类型未注册（每次都失败并计入 dropped），
 *        槽 1 事件类型正常注册，验证槽 1 仍正常发布且 next_us 前进。
 */
void test_ticker_non_overflow_publish_error_does_not_block_other_slots(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms-unregistered" },
        { 10u, TICKER_EVT2, 1u, "10ms-ok" },
    };
    bm_event_subscriber_id_t id2;

    /* 故意不注册 TICKER_EVT，只注册 TICKER_EVT2。 */
    bm_event_register_type(TICKER_EVT2, "TICK2");
    bm_event_subscribe(TICKER_EVT2, ticker_cb2, NULL, &id2);
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 2u));

    bm_hal_uptime_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
    TEST_ASSERT_GREATER_THAN(0u, bm_ticker_get_dropped(0u));
    TEST_ASSERT_EQUAL(0u, bm_ticker_get_dropped(1u));
    TEST_ASSERT_EQUAL(1, bm_event_process(4));
    TEST_ASSERT_EQUAL(1, g_event_count2);

    /* 第二轮：槽 0 next_us 应已推进（未卡死），槽 1 继续正常发布。 */
    bm_hal_uptime_native_advance_us(10000u);
    TEST_ASSERT_EQUAL(1, bm_ticker_poll());
    TEST_ASSERT_EQUAL(2u, bm_ticker_get_dropped(0u));
    TEST_ASSERT_EQUAL(1, bm_event_process(4));
    TEST_ASSERT_EQUAL(2, g_event_count2);

    bm_event_unsubscribe(TICKER_EVT2, id2);
}

/**
 * @brief H2 回归：单次 poll 内落后多个周期、内层 while 因（队列满或
 *        catchup 预算 BM_CONFIG_TICKER_MAX_CATCHUP）而提前退出后，
 *        resync 之前若仍落后（now >= next_us），必须把剩余全部欠账
 *        周期数一次性计入 dropped，口径与 OVERFLOW 单次丢弃一致，
 *        而不是只计最后一次失败、静默丢弃其余欠账（旧 bug）。
 *
 * period=10ms，一次性推进 6 个周期（60ms）。事件队列该优先级可用容量
 * 为 3（BM_CONFIG_EVENT_QUEUE_SIZE/PRIORITIES - 1 个保留槽），故前 3 个
 * 周期发布成功，第 4 次因队列满触发 break，resync 时应把第 4~6 三个
 * 周期全部计入 dropped（旧 bug 只会计 1）。
 */
void test_ticker_multi_period_backlog_counts_all_skipped_as_dropped(void) {
    static const bm_ticker_slot_t slots[] = {
        { 10u, TICKER_EVT, 1u, "10ms" },
    };

    bm_event_register_type(TICKER_EVT, "TICK");
    TEST_ASSERT_EQUAL(BM_OK, bm_ticker_init(slots, 1u));

    bm_hal_uptime_native_advance_us(60000u);
    TEST_ASSERT_EQUAL(3, bm_ticker_poll());
    TEST_ASSERT_EQUAL_UINT32(3u, bm_ticker_get_dropped(0u));
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
    RUN_TEST(test_ticker_non_overflow_publish_error_counts_as_dropped);
    RUN_TEST(test_ticker_non_overflow_publish_error_does_not_block_other_slots);
    RUN_TEST(test_ticker_multi_period_backlog_counts_all_skipped_as_dropped);
    return UNITY_END();
}

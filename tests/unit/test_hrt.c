/**
 * @file test_hrt.c
 * @brief 硬实时调度器（HRT）多槽、截止期与边界条件单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "bm_hal_timer_native.h"

static uint32_t g_slot_a;
static uint32_t g_slot_b;
static void slot_a_cb(void *context) {
    (void)context;
    g_slot_a++;
}

static void slot_b_cb(void *context) {
    (void)context;
    g_slot_b++;
}

void setUp(void) {
    BM_LOGI("test_hrt", "setUp: reset HRT and native timer");
    g_slot_a = 0u;
    g_slot_b = 0u;
    bm_hal_timer_native_reset_ticks();
    bm_hal_timer_native_set_init_result(BM_OK);
    bm_hrt_reset();
}

void tearDown(void) {
    bm_hrt_reset();
}

void test_hrt_schedules_multiple_slots(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
        { 2000u, BM_HRT_TRIGGER_TIMER, slot_b_cb, NULL, "b" },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 2u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());
    TEST_ASSERT_EQUAL(1, bm_hrt_is_started());

    bm_hal_timer_native_advance_ticks(10u);
    TEST_ASSERT_EQUAL(1u, g_slot_a);
    TEST_ASSERT_EQUAL(0u, g_slot_b);

    bm_hal_timer_native_advance_ticks(10u);
    TEST_ASSERT_EQUAL(2u, g_slot_a);
    TEST_ASSERT_EQUAL(1u, g_slot_b);
}

void test_hrt_deadline_miss(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());

    bm_hal_timer_native_jump_ticks(25u);
    TEST_ASSERT_EQUAL(0u, g_slot_a);
    TEST_ASSERT_GREATER_THAN(0u, bm_hrt_get_deadline_missed(0u));
    TEST_ASSERT_EQUAL(bm_hrt_get_deadline_missed(0u),
                      bm_hrt_get_deadline_missed_total());
}

void test_hrt_rejects_invalid_period(void) {
    static const bm_hrt_slot_t slots[] = {
        { 150u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "bad" },
    };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_hrt_init(slots, 1u));
}

void test_hrt_stop_clears_callback(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());
    bm_hrt_stop();
    TEST_ASSERT_EQUAL(0, bm_hrt_is_started());
    bm_hal_timer_native_advance_ticks(20u);
    TEST_ASSERT_EQUAL(0u, g_slot_a);
}

void test_hrt_start_requires_init(void) {
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hrt_start());
}

void test_hrt_start_ok_with_initialized_zero_slots(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(NULL, 0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());
    bm_hrt_stop();
}

void test_hrt_propagates_hal_init_error(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    bm_hal_timer_native_set_init_result(BM_ERR_BUSY);
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_hrt_start());
    TEST_ASSERT_EQUAL(0, bm_hrt_is_started());
}

void test_hrt_tick_wraparound(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
    };

    bm_hal_timer_native_jump_ticks(0xFFFFFFFBu);
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());
    bm_hal_timer_native_advance_ticks(9u);
    TEST_ASSERT_EQUAL(0u, g_slot_a);
    bm_hal_timer_native_advance_ticks(1u);
    TEST_ASSERT_EQUAL(1u, g_slot_a);
    bm_hal_timer_native_advance_ticks(9u);
    TEST_ASSERT_EQUAL(1u, g_slot_a);
    bm_hal_timer_native_advance_ticks(1u);
    TEST_ASSERT_EQUAL(2u, g_slot_a);
}

void test_hrt_rejects_init_while_started(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_hrt_init(slots, 1u));
    bm_hrt_stop();
}

void test_hrt_rejects_slot_overflow(void) {
    static const bm_hrt_slot_t slot = {
        1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a"
    };

    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
                      bm_hrt_init(&slot, BM_CONFIG_HRT_MAX_SLOTS + 1u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hrt_schedules_multiple_slots);
    RUN_TEST(test_hrt_deadline_miss);
    RUN_TEST(test_hrt_rejects_invalid_period);
    RUN_TEST(test_hrt_stop_clears_callback);
    RUN_TEST(test_hrt_start_requires_init);
    RUN_TEST(test_hrt_start_ok_with_initialized_zero_slots);
    RUN_TEST(test_hrt_propagates_hal_init_error);
    RUN_TEST(test_hrt_tick_wraparound);
    RUN_TEST(test_hrt_rejects_slot_overflow);
    RUN_TEST(test_hrt_rejects_init_while_started);
    return UNITY_END();
}

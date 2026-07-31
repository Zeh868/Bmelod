/**
 * @file test_hrt.c
 * @brief 硬实时调度器（HRT）多槽、截止期与边界条件单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-31
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-07-31       1.1            zeh            补端到端接线用例：hrt_dispatch
 *                                                驱动的槽回调内 bm_in_hrt_isr()==1
 */

#include "unity.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "bm_hal_timer_native.h"
#include "bm_critical_wrap.h"

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

/* 端到端接线观测：hrt_dispatch 驱动的槽回调内是否处于 HRT ISR 上下文 */
static int g_cb_in_hrt_isr_seen;
static void slot_ctx_probe_cb(void *context) {
    (void)context;
    g_cb_in_hrt_isr_seen = bm_in_hrt_isr();
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

/**
 * @brief H3 回归：一次 ISR 内连错多个周期时，deadline_missed 须按实际
 *        错过的周期数累加，而非固定 +1（会严重低估真实错失程度）。
 *
 * period_us=1000，BM_CONFIG_HRT_TICK_US=100 ⇒ period_ticks=10。
 * 一次性 jump 到 tick 55：到期周期依次为 10/20/30/40/50，共 5 个应计入
 * deadline_missed；tick 60 尚未到期。
 */
void test_hrt_deadline_miss_counts_all_missed_periods(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_a_cb, NULL, "a" },
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());

    bm_hal_timer_native_jump_ticks(55u);
    TEST_ASSERT_EQUAL(0u, g_slot_a);
    TEST_ASSERT_EQUAL_UINT32(5u, bm_hrt_get_deadline_missed(0u));
    TEST_ASSERT_EQUAL_UINT32(5u, bm_hrt_get_deadline_missed_total());
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

/**
 * @brief 端到端接线断言：hrt_dispatch 驱动的槽回调必须运行在
 *        bm_hrt_isr_enter/exit 标记的 HRT ISR 上下文内（掩码模式对
 *        event/ultra/mempool 的 fail-closed 拦截依赖该标记），
 *        且回调返回后上下文已退出。非掩码默认配置下计数同样维护，
 *        本断言在两档配置下均有效。
 */
void test_hrt_dispatch_marks_hrt_isr_context(void) {
    static const bm_hrt_slot_t slots[] = {
        { 1000u, BM_HRT_TRIGGER_TIMER, slot_ctx_probe_cb, NULL, "probe" },
    };

    g_cb_in_hrt_isr_seen = -1;
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_init(slots, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hrt_start());

    TEST_ASSERT_EQUAL(0, bm_in_hrt_isr());
    bm_hal_timer_native_advance_ticks(10u);
    TEST_ASSERT_EQUAL(1, g_cb_in_hrt_isr_seen);
    TEST_ASSERT_EQUAL(0, bm_in_hrt_isr());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hrt_schedules_multiple_slots);
    RUN_TEST(test_hrt_deadline_miss);
    RUN_TEST(test_hrt_deadline_miss_counts_all_missed_periods);
    RUN_TEST(test_hrt_rejects_invalid_period);
    RUN_TEST(test_hrt_stop_clears_callback);
    RUN_TEST(test_hrt_start_requires_init);
    RUN_TEST(test_hrt_start_ok_with_initialized_zero_slots);
    RUN_TEST(test_hrt_propagates_hal_init_error);
    RUN_TEST(test_hrt_tick_wraparound);
    RUN_TEST(test_hrt_rejects_slot_overflow);
    RUN_TEST(test_hrt_rejects_init_while_started);
    RUN_TEST(test_hrt_dispatch_marks_hrt_isr_context);
    return UNITY_END();
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_wcet_mon.c
 * @brief bm_wcet_mon 单元测试：注册/迭代/计时/预算/sink/misuse
 */
#include "unity.h"
#include "bm/hybrid/bm_wcet_mon.h"
#include "bm_types.h"
#include "bm_hal_uptime_native.h"

void setUp(void) {
    bm_hal_uptime_native_reset();
    bm_wcet_mon_init();
}

void tearDown(void) {}

/* ---- Task 1：注册与迭代 ---- */

void test_register_rejects_null(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_wcet_mon_register(NULL));
}

void test_register_rejects_duplicate(void) {
    static BM_WCET_SPAN_DEFINE(sp_dup, 100u);
    TEST_ASSERT_EQUAL(BM_OK, bm_wcet_mon_register(&sp_dup));
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_wcet_mon_register(&sp_dup));
}

void test_register_full_returns_no_mem(void) {
    static bm_wcet_span_t sp[BM_CONFIG_WCET_MON_MAX_SPANS + 1u];
    for (uint32_t i = 0u; i < BM_CONFIG_WCET_MON_MAX_SPANS; ++i) {
        sp[i].name = "x"; sp[i].budget_us = 0u;
        TEST_ASSERT_EQUAL(BM_OK, bm_wcet_mon_register(&sp[i]));
    }
    sp[BM_CONFIG_WCET_MON_MAX_SPANS].name = "y";
    TEST_ASSERT_EQUAL(BM_ERR_NO_MEM,
        bm_wcet_mon_register(&sp[BM_CONFIG_WCET_MON_MAX_SPANS]));
}

void test_iteration_returns_registered_spans(void) {
    static BM_WCET_SPAN_DEFINE(sp_a, 10u);
    static BM_WCET_SPAN_DEFINE(sp_b, 20u);
    TEST_ASSERT_EQUAL(0u, bm_wcet_mon_span_count());
    TEST_ASSERT_EQUAL(BM_OK, bm_wcet_mon_register(&sp_a));
    TEST_ASSERT_EQUAL(BM_OK, bm_wcet_mon_register(&sp_b));
    TEST_ASSERT_EQUAL(2u, bm_wcet_mon_span_count());
    TEST_ASSERT_EQUAL_PTR(&sp_a, bm_wcet_mon_span_at(0u));
    TEST_ASSERT_EQUAL_PTR(&sp_b, bm_wcet_mon_span_at(1u));
    TEST_ASSERT_NULL(bm_wcet_mon_span_at(2u));
}

/* ---- Task 2：计时与预算 ---- */

void test_begin_end_measures_elapsed(void) {
    static BM_WCET_SPAN_DEFINE(sp_meas, 0u);
    bm_wcet_mon_begin(&sp_meas);
    bm_hal_uptime_native_advance_us(150u);
    bm_wcet_mon_end(&sp_meas);
    TEST_ASSERT_EQUAL_UINT32(150u, sp_meas.last_us);
    TEST_ASSERT_EQUAL_UINT32(150u, sp_meas.max_us);
    TEST_ASSERT_EQUAL_UINT32(1u, sp_meas.run_count);
    TEST_ASSERT_EQUAL_UINT32(0u, sp_meas.overrun_count);

    bm_wcet_mon_begin(&sp_meas);
    bm_hal_uptime_native_advance_us(80u);
    bm_wcet_mon_end(&sp_meas);
    TEST_ASSERT_EQUAL_UINT32(80u, sp_meas.last_us);
    TEST_ASSERT_EQUAL_UINT32(150u, sp_meas.max_us); /* max 单调增 */
    TEST_ASSERT_EQUAL_UINT32(2u, sp_meas.run_count);
}

void test_budget_overrun_counted(void) {
    static BM_WCET_SPAN_DEFINE(sp_over, 100u);
    bm_wcet_mon_begin(&sp_over);
    bm_hal_uptime_native_advance_us(101u);
    bm_wcet_mon_end(&sp_over);
    TEST_ASSERT_EQUAL_UINT32(1u, sp_over.overrun_count);

    bm_wcet_mon_begin(&sp_over);
    bm_hal_uptime_native_advance_us(100u); /* == 预算，不算超（严格 >） */
    bm_wcet_mon_end(&sp_over);
    TEST_ASSERT_EQUAL_UINT32(1u, sp_over.overrun_count);
}

void test_budget_zero_never_overruns(void) {
    static BM_WCET_SPAN_DEFINE(sp_free, 0u);
    bm_wcet_mon_begin(&sp_free);
    bm_hal_uptime_native_advance_us(1000000u);
    bm_wcet_mon_end(&sp_free);
    TEST_ASSERT_EQUAL_UINT32(0u, sp_free.overrun_count);
    TEST_ASSERT_EQUAL_UINT32(1000000u, sp_free.last_us);
}

void test_end_without_begin_counts_misuse(void) {
    static BM_WCET_SPAN_DEFINE(sp_mis1, 0u);
    bm_wcet_mon_end(&sp_mis1);
    TEST_ASSERT_EQUAL_UINT32(1u, sp_mis1.misuse_count);
    TEST_ASSERT_EQUAL_UINT32(0u, sp_mis1.run_count); /* 不更新统计 */
}

void test_double_begin_counts_misuse_and_restarts(void) {
    static BM_WCET_SPAN_DEFINE(sp_mis2, 0u);
    bm_wcet_mon_begin(&sp_mis2);
    bm_hal_uptime_native_advance_us(50u);
    bm_wcet_mon_begin(&sp_mis2); /* misuse + 覆盖 t0 */
    bm_hal_uptime_native_advance_us(30u);
    bm_wcet_mon_end(&sp_mis2);
    TEST_ASSERT_EQUAL_UINT32(1u, sp_mis2.misuse_count);
    TEST_ASSERT_EQUAL_UINT32(30u, sp_mis2.last_us); /* 从第二次 begin 起算 */
}

void test_null_span_is_noop(void) {
    bm_wcet_mon_begin(NULL);
    bm_wcet_mon_end(NULL);
    bm_wcet_mon_report_miss(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_register_rejects_null);
    RUN_TEST(test_register_rejects_duplicate);
    RUN_TEST(test_register_full_returns_no_mem);
    RUN_TEST(test_iteration_returns_registered_spans);
    RUN_TEST(test_begin_end_measures_elapsed);
    RUN_TEST(test_budget_overrun_counted);
    RUN_TEST(test_budget_zero_never_overruns);
    RUN_TEST(test_end_without_begin_counts_misuse);
    RUN_TEST(test_double_begin_counts_misuse_and_restarts);
    RUN_TEST(test_null_span_is_noop);
    return UNITY_END();
}

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_register_rejects_null);
    RUN_TEST(test_register_rejects_duplicate);
    RUN_TEST(test_register_full_returns_no_mem);
    RUN_TEST(test_iteration_returns_registered_spans);
    return UNITY_END();
}

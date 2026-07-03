/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_tt_schedule_json.c
 * @brief Unit tests for bm_tt_schedule_report_json (schedule-map v2 Task 2, schema v1)
 *
 * @details Assembles one harmonic-period schedule table (fast every=1 at=0 wcet=50 / mid
 * every=5 at=0 wcet=50 / slow every=10 at=9 wcet=50, all ISR-domain; tele every=10 at=0
 * wcet=200, MAINLOOP-domain; minor_us=1000, n_frames=LCM(1,5,10,10)=10), following the same
 * BM_BUS_DEFINE/BM_LET_DEFINE_ISR/BM_LET_DEFINE_MAINLOOP/BM_SCHEDULE_DEFINE assembly style as
 * `tests/tools/tt_schedule_map_dump.c` and `tests/unit/test_tt_schedule.c` scenario 11. Three
 * cases:
 *   1. Key facts with an explicit meta (cpu/ref_clk_hz/operating_points_hz) — asserts
 *      schema_version/n_frames/hyperperiod_us/ref_clk_hz/operating_points_hz and the exact
 *      frame-0 and frame-9 load lines (isr_load_us/mainloop_pending_us), plus the reserved
 *      empty edges array.
 *   2. meta == NULL falls back to an all-zero default (cpu=0/ref_clk_hz=0/no operating points).
 *   3. Determinism: the same schedule table emitted twice yields byte-identical output.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change Log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 2: initial JSON export tests
 *
 */
#include "unity.h"
#include "bm_tt_schedule.h"
#include "bm_bus.h"

#include <string.h>

/* =========================================================================
 * Fixture: harmonic-period schedule table (fast/mid/slow ISR + tele MAINLOOP)
 * ========================================================================= */

BM_BUS_DEFINE(json_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_fast_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_mid_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_slow_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(json_tele_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_json_in_bus;
static bm_bus_t g_json_fast_out_bus;
static bm_bus_t g_json_mid_out_bus;
static bm_bus_t g_json_slow_out_bus;
static bm_bus_t g_json_tele_out_bus;

static const uint32_t k_json_in_safe = 0u;
static const uint32_t k_json_out_safe = 0u;

/** @brief no-op step: this test suite only exercises bm_tt_schedule_report_json's text output */
static void json_noop_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_json_inputs[] = {
    { .bus = &g_json_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_json_in_safe },
};
static const bm_let_output_t k_json_fast_outputs[] = {
    { .bus = &g_json_fast_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};
static const bm_let_output_t k_json_mid_outputs[] = {
    { .bus = &g_json_mid_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};
static const bm_let_output_t k_json_slow_outputs[] = {
    { .bus = &g_json_slow_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};
static const bm_let_output_t k_json_tele_outputs[] = {
    { .bus = &g_json_tele_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_json_out_safe },
};

BM_LET_DEFINE_ISR(task_json_fast, 1u, 0u, 50u, json_noop_step, NULL,
                   k_json_inputs, k_json_fast_outputs);
BM_LET_DEFINE_ISR(task_json_mid, 5u, 0u, 50u, json_noop_step, NULL,
                   k_json_inputs, k_json_mid_outputs);
BM_LET_DEFINE_ISR(task_json_slow, 10u, 9u, 50u, json_noop_step, NULL,
                   k_json_inputs, k_json_slow_outputs);
BM_LET_DEFINE_MAINLOOP(task_json_tele, 10u, 0u, 200u, json_noop_step, NULL,
                        k_json_inputs, k_json_tele_outputs);
BM_SCHEDULE_DEFINE(sched_json, 1000u, &task_json_fast, &task_json_mid,
                    &task_json_slow, &task_json_tele);

/** @brief Operating-point frequency array used by test case 1's meta */
static const uint32_t k_json_ops[] = { 240000000u, 80000000u };

/* =========================================================================
 * Line capture helper
 * ========================================================================= */

static char g_json_buf[8192];

/** @brief emit callback: append each line + '\n' into the static capture buffer g_json_buf */
static void json_emit(const char *line, void *u) {
    (void)u;
    (void)strncat(g_json_buf, line, sizeof(g_json_buf) - strlen(g_json_buf) - 2u);
    (void)strncat(g_json_buf, "\n", sizeof(g_json_buf) - strlen(g_json_buf) - 1u);
}

/** @brief Unity per-test hook (unused: each test manages its own bus open/close) */
void setUp(void) {
}

/** @brief Unity per-test hook (unused: each test manages its own bus open/close) */
void tearDown(void) {
}

/**
 * @brief Case 1: explicit meta -> assert key schema fields, per-task facts and per-frame loads
 */
void test_report_json_key_facts_with_explicit_meta(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_tt_schedule_json_meta_t meta = { 0u, 240000000u, k_json_ops, 2u };

    g_json_buf[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"schema_version\": 1"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"n_frames\": 10"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"hyperperiod_us\": 10000"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"ref_clk_hz\": 240000000"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"operating_points_hz\": [240000000, 80000000]"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf,
        "{\"t\": 0, \"isr_load_us\": 100, \"mainloop_pending_us\": 200}"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf,
        "{\"t\": 9, \"isr_load_us\": 100, \"mainloop_pending_us\": 0}"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"edges\": []"));

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

/**
 * @brief Case 2: meta == NULL falls back to an all-zero default
 */
void test_report_json_null_meta_defaults_to_zero(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    g_json_buf[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    bm_tt_schedule_report_json(&sched_json, NULL, json_emit, NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"cpu\": 0"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"ref_clk_hz\": 0"));
    TEST_ASSERT_NOT_NULL(strstr(g_json_buf, "\"operating_points_hz\": []"));

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

/**
 * @brief Case 3: determinism -- the same schedule table emitted twice yields identical output
 */
void test_report_json_deterministic_across_two_runs(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_tt_schedule_json_meta_t meta = { 0u, 240000000u, k_json_ops, 2u };
    static char buf_a[8192];
    static char buf_b[8192];

    buf_a[0] = '\0';
    buf_b[0] = '\0';

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_in_bus, &json_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_fast_out_bus, &json_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_mid_out_bus, &json_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_slow_out_bus, &json_slow_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_json_tele_out_bus, &json_tele_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_json));

    /* json_emit always appends to the shared g_json_buf regardless of u; reset it around each
     * run and snapshot the result into a per-run buffer before comparing the two runs. */
    g_json_buf[0] = '\0';
    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);
    (void)strncpy(buf_a, g_json_buf, sizeof(buf_a) - 1u);
    buf_a[sizeof(buf_a) - 1u] = '\0';

    g_json_buf[0] = '\0';
    bm_tt_schedule_report_json(&sched_json, &meta, json_emit, NULL);
    (void)strncpy(buf_b, g_json_buf, sizeof(buf_b) - 1u);
    buf_b[sizeof(buf_b) - 1u] = '\0';

    TEST_ASSERT_EQUAL_STRING(buf_a, buf_b);

    bm_bus_close(&g_json_in_bus);
    bm_bus_close(&g_json_fast_out_bus);
    bm_bus_close(&g_json_mid_out_bus);
    bm_bus_close(&g_json_slow_out_bus);
    bm_bus_close(&g_json_tele_out_bus);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_json_key_facts_with_explicit_meta);
    RUN_TEST(test_report_json_null_meta_defaults_to_zero);
    RUN_TEST(test_report_json_deterministic_across_two_runs);
    return UNITY_END();
}

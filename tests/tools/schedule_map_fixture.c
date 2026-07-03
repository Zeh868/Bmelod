/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture.c
 * @brief Two-table harmonic fixture assembly (schedule-map v2 host-dump tests)
 *
 * @details Assembly follows the same self-contained pattern as the earlier
 * tests/tools/tt_schedule_map_dump.c: BM_BUS_DEFINE + static bm_bus_t +
 * input/output binding tables + empty step functions. This fixture only
 * cares about bm_tt_schedule_report()/bm_tt_schedule_report_json() text
 * output, not real dataflow, so every step is a no-op.
 *
 * Table A "sched_fixture_a" (minor_us=1000): harmonic three-task ISR set
 * (fast every=1 at=0 wcet=50 / mid every=5 at=0 wcet=50 / slow every=10
 * at=9 wcet=50) plus one MAINLOOP task (tele every=10 at=0 wcet=200).
 *
 * Table B "sched_fixture_b" (minor_us=2000): a single ISR task
 * (solo every=2 at=1 wcet=80).
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3: two-table fixture
 *
 */
#include "schedule_map_fixture.h"
#include "bm_bus.h"

/* ---- Table A buses ---- */
BM_BUS_DEFINE(fxa_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(fxa_fast_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(fxa_mid_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(fxa_slow_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(fxa_tele_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_fxa_in_bus;
static bm_bus_t g_fxa_fast_out_bus;
static bm_bus_t g_fxa_mid_out_bus;
static bm_bus_t g_fxa_slow_out_bus;
static bm_bus_t g_fxa_tele_out_bus;

/* ---- Table B buses ---- */
BM_BUS_DEFINE(fxb_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(fxb_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_fxb_in_bus;
static bm_bus_t g_fxb_out_bus;

static const uint32_t k_fx_in_safe = 0u;
static const uint32_t k_fx_out_safe = 0u;

/** @brief No-op step: this fixture only cares about report() text output, not dataflow */
static void fx_noop_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_fxa_inputs[] = {
    { .bus = &g_fxa_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_fx_in_safe },
};
static const bm_let_output_t k_fxa_fast_outputs[] = {
    { .bus = &g_fxa_fast_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_fx_out_safe },
};
static const bm_let_output_t k_fxa_mid_outputs[] = {
    { .bus = &g_fxa_mid_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_fx_out_safe },
};
static const bm_let_output_t k_fxa_slow_outputs[] = {
    { .bus = &g_fxa_slow_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_fx_out_safe },
};
static const bm_let_output_t k_fxa_tele_outputs[] = {
    { .bus = &g_fxa_tele_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_fx_out_safe },
};

BM_LET_DEFINE_ISR(task_fxa_fast, 1u, 0u, 50u, fx_noop_step, NULL,
                   k_fxa_inputs, k_fxa_fast_outputs);
BM_LET_DEFINE_ISR(task_fxa_mid, 5u, 0u, 50u, fx_noop_step, NULL,
                   k_fxa_inputs, k_fxa_mid_outputs);
BM_LET_DEFINE_ISR(task_fxa_slow, 10u, 9u, 50u, fx_noop_step, NULL,
                   k_fxa_inputs, k_fxa_slow_outputs);
BM_LET_DEFINE_MAINLOOP(task_fxa_tele, 10u, 0u, 200u, fx_noop_step, NULL,
                        k_fxa_inputs, k_fxa_tele_outputs);
BM_SCHEDULE_DEFINE(sched_fixture_a, 1000u,
                    &task_fxa_fast, &task_fxa_mid, &task_fxa_slow, &task_fxa_tele);

static const bm_let_input_t k_fxb_inputs[] = {
    { .bus = &g_fxb_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_fx_in_safe },
};
static const bm_let_output_t k_fxb_outputs[] = {
    { .bus = &g_fxb_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_fx_out_safe },
};

BM_LET_DEFINE_ISR(task_fxb_solo, 2u, 1u, 80u, fx_noop_step, NULL,
                   k_fxb_inputs, k_fxb_outputs);
BM_SCHEDULE_DEFINE(sched_fixture_b, 2000u, &task_fxb_solo);

int schedule_map_fixture_setup(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    if (bm_bus_open(&g_fxa_in_bus, &fxa_in_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_fxa_fast_out_bus, &fxa_fast_out_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_fxa_mid_out_bus, &fxa_mid_out_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_fxa_slow_out_bus, &fxa_slow_out_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_fxa_tele_out_bus, &fxa_tele_out_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_fxb_in_bus, &fxb_in_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_fxb_out_bus, &fxb_out_bus_storage, &cfg) != BM_OK) {
        return 1;
    }
    return 0;
}

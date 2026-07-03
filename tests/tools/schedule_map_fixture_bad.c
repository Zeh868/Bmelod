/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture_bad.c
 * @brief Self-contained overload table + register unit: the negative-gate
 * fixture for bm_schedule_map_dump_bad
 *
 * @details Declares a single ISR task whose wcet_us (5000) exceeds its
 * table's minor_us (1000), so bm_tt_schedule_init() must reject it with
 * BM_ERR_INVALID. bm_schedule_map_main.c treats any non-BM_OK init() as a
 * hard build-gate failure (stderr + return 1); this file is what proves
 * that gate actually fires end-to-end via ctest's WILL_FAIL property on
 * the schedule_map_dump_gate test.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3: gate negative fixture
 *
 */
#include "bm_schedule_map_reg.h"
#include "bm_bus.h"

BM_BUS_DEFINE(bad_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(bad_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_bad_in_bus;
static bm_bus_t g_bad_out_bus;

static const uint32_t k_bad_in_safe = 0u;
static const uint32_t k_bad_out_safe = 0u;

/** @brief No-op step: init() is expected to reject this table before any step ever runs */
static void bad_noop_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_bad_inputs[] = {
    { .bus = &g_bad_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_bad_in_safe },
};
static const bm_let_output_t k_bad_outputs[] = {
    { .bus = &g_bad_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_bad_out_safe },
};

/* wcet_us=5000 > minor_us=1000: bm_tt_schedule_init() must reject this. */
BM_LET_DEFINE_ISR(task_bad_overload, 1u, 0u, 5000u, bad_noop_step, NULL,
                   k_bad_inputs, k_bad_outputs);
BM_SCHEDULE_DEFINE(sched_fixture_bad, 1000u, &task_bad_overload);

const bm_schedule_map_entry_t g_bm_schedule_map_entries[] = {
    { &sched_fixture_bad, 0u },
};
const uint32_t g_bm_schedule_map_entry_count = 1u;
const uint32_t g_bm_schedule_map_ref_clk_hz  = 0u;
const uint32_t g_bm_schedule_map_op_points_hz[] = { 0u };
const uint32_t g_bm_schedule_map_op_point_count = 0u;

int bm_schedule_map_setup(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    if (bm_bus_open(&g_bad_in_bus, &bad_in_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_bad_out_bus, &bad_out_bus_storage, &cfg) != BM_OK) {
        return 1;
    }
    return 0;
}

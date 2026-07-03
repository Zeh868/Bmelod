/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture_reg.c
 * @brief Hand-written register unit sample for IDE tier-2/3, equivalent to
 * bm_add_schedule_map() generated output
 *
 * @details Points at the two-table fixture in schedule_map_fixture.c and
 * demonstrates the exact contract a CMake-generated register unit
 * (Task 5, bm_add_schedule_map()) must also satisfy.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3: hand-written register unit sample
 *
 */
#include "bm_schedule_map_reg.h"
#include "schedule_map_fixture.h"

const bm_schedule_map_entry_t g_bm_schedule_map_entries[] = {
    { &sched_fixture_a, 0u },
    { &sched_fixture_b, 1u },
};
const uint32_t g_bm_schedule_map_entry_count = 2u;
const uint32_t g_bm_schedule_map_ref_clk_hz  = 240000000u;
const uint32_t g_bm_schedule_map_op_points_hz[] = { 240000000u, 80000000u };
const uint32_t g_bm_schedule_map_op_point_count = 2u;

int bm_schedule_map_setup(void) { return schedule_map_fixture_setup(); }

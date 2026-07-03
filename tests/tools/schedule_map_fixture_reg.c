/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture_reg.c
 * @brief IDE 二/三档手写注册单元样例，等价于 bm_add_schedule_map() 生成的产物
 *
 * @details 指向 schedule_map_fixture.c 中的双表装配件，演示 CMake 生成的
 * 注册单元（Task 5，bm_add_schedule_map()）同样必须满足的契约细节。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：手写注册单元样例初版
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

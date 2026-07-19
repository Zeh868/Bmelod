/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture_reg.c
 * @brief IDE 二/三档手写注册单元样例，等价于 bm_add_schedule_map() 生成的产物
 *
 * @details 指向 schedule_map_fixture.c 中的双表装配件，演示 CMake 生成的
 * 注册单元（Task 5，bm_add_schedule_map()）同样必须满足的契约细节。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：手写注册单元样例初版
 * 2026-07-04       1.1            zeh            Task 6：声明 2 个干扰源
 *                                                 （spi_isr@hardware、
 *                                                 wifi_task@scheduled，均
 *                                                 cpu0），演示 main 按 cpu
 *                                                 过滤——sched_fixture_a
 *                                                 （cpu0）的出表 JSON 含二者，
 *                                                 sched_fixture_b（cpu1）不含
 * 2026-07-04       1.2            zeh            Task 4：干扰源数组包 #ifdef
 *                                                 BM_CONFIG_SM_INTERFERENCE_SRC
 *                                                 分支，演示 config 单源展开与
 *                                                 显式数组两条路都合法（本测试
 *                                                 构建未定义该宏，走既有显式
 *                                                 分支，数值不变）
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

#ifdef BM_CONFIG_SM_INTERFERENCE_SRC
/* config 单源：应用工程推荐写法（bm_config_app.h 定义宏后此分支生效） */
const bm_schedule_map_interference_t g_bm_schedule_map_interference[] =
    BM_CONFIG_SM_INTERFERENCE_SRC;
const uint32_t g_bm_schedule_map_interference_count =
    (uint32_t)(sizeof(g_bm_schedule_map_interference) / sizeof(g_bm_schedule_map_interference[0]));
#else
/* 显式数组：本 fixture 的测试基准（既有 2 源，勿改数值） */
const bm_schedule_map_interference_t g_bm_schedule_map_interference[] = {
    { { "spi_isr",   1000u, 20u,  0u }, 0u }, /* hardware，cpu0 */
    { { "wifi_task", 5000u, 300u, 1u }, 0u }, /* scheduled，cpu0 */
};
const uint32_t g_bm_schedule_map_interference_count = 2u;
#endif

int bm_schedule_map_setup(void) { return schedule_map_fixture_setup(); }

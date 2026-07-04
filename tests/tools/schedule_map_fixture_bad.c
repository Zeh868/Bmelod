/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file schedule_map_fixture_bad.c
 * @brief 自包含超载表 + 注册单元：bm_schedule_map_dump_bad 的负例门禁装配件
 *
 * @details 声明单个 ISR 任务，其 wcet_us（5000）超过所属表的 minor_us
 * （1000），因此 bm_tt_schedule_init() 必须以 BM_ERR_INVALID 拒绝它。
 * bm_schedule_map_main.c 把任何非 BM_OK 的 init() 视为硬构建门禁失败
 * （stderr + return 1）；本文件正是用来证明该门禁经 ctest 在
 * schedule_map_dump_gate 测试上的 WILL_FAIL 属性端到端确实触发。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-04
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：门禁负例装配件初版
 * 2026-07-04       1.1            zeh            Task 6：补 reg 契约新增的
 *                                                 干扰源声明数组两个 extern
 *                                                 （空数组占位 + count 0），
 *                                                 门禁负例本身不关心干扰源
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

/** @brief 空操作 step：预期 init() 会在任何 step 跑起来之前就拒绝这张表 */
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

/* wcet_us=5000 > minor_us=1000：bm_tt_schedule_init() 必须拒绝这张表 */
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
const bm_schedule_map_interference_t g_bm_schedule_map_interference[] = {
    { { "", 0u, 0u, 0u }, 0u },
};
const uint32_t g_bm_schedule_map_interference_count = 0u;

int bm_schedule_map_setup(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    if (bm_bus_open(&g_bad_in_bus, &bad_in_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_bad_out_bus, &bad_out_bus_storage, &cfg) != BM_OK) {
        return 1;
    }
    return 0;
}

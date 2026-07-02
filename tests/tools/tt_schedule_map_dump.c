/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file tt_schedule_map_dump.c
 * @brief host dump 工具：装配示例调度表，导出 bm_tt_schedule_report 的
 * 调度概览报告到文件
 *
 * @details 示例调度表装配思路取自 tests/unit/test_tt_schedule.c 场景 11
 * “谐波周期”（fast every=1 at=0 / mid every=5 at=0 / slow every=10 at=9，
 * minor_us=1000，wcet_us 各 50），但本文件不 include 测试文件——工具程序
 * 自包含、独立可编译，用同样的 BM_BUS_DEFINE/BM_LET_DEFINE_ISR/
 * BM_SCHEDULE_DEFINE 宏重新声明一份等价装配。step 函数为空，本工具只关心
 * bm_tt_schedule_report 的文本输出，不关心真实数据流。
 *
 * 用法：tt_schedule_map_dump <输出文件路径>
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            Task 7：新增 host dump 工具，
 *                                                 CMake schedule-map 目标生成 schedule_map.txt
 *
 */
#include "bm_tt_schedule.h"
#include "bm_bus.h"

#include <stdio.h>

BM_BUS_DEFINE(dump_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(dump_fast_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(dump_mid_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(dump_slow_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_dump_in_bus;
static bm_bus_t g_dump_fast_out_bus;
static bm_bus_t g_dump_mid_out_bus;
static bm_bus_t g_dump_slow_out_bus;

static const uint32_t k_dump_in_safe = 0u;
static const uint32_t k_dump_out_safe = 0u;

/** @brief 空 step：本工具只关心 report 的文本输出，不关心真实数据流 */
static void dump_noop_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_dump_inputs[] = {
    { .bus = &g_dump_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_dump_in_safe },
};
static const bm_let_output_t k_dump_fast_outputs[] = {
    { .bus = &g_dump_fast_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_dump_out_safe },
};
static const bm_let_output_t k_dump_mid_outputs[] = {
    { .bus = &g_dump_mid_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_dump_out_safe },
};
static const bm_let_output_t k_dump_slow_outputs[] = {
    { .bus = &g_dump_slow_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_dump_out_safe },
};

BM_LET_DEFINE_ISR(task_dump_fast, 1u, 0u, 50u, dump_noop_step, NULL,
                   k_dump_inputs, k_dump_fast_outputs);
BM_LET_DEFINE_ISR(task_dump_mid, 5u, 0u, 50u, dump_noop_step, NULL,
                   k_dump_inputs, k_dump_mid_outputs);
BM_LET_DEFINE_ISR(task_dump_slow, 10u, 9u, 50u, dump_noop_step, NULL,
                   k_dump_inputs, k_dump_slow_outputs);
BM_SCHEDULE_DEFINE(sched_dump, 1000u, &task_dump_fast, &task_dump_mid, &task_dump_slow);

/** @brief emit 回调：把每行文本写入 argv[1] 指定的输出文件（u 即该 FILE*） */
static void dump_emit(const char *line, void *u) {
    FILE *fp = (FILE *)u;

    fprintf(fp, "%s\n", line);
}

int main(int argc, char **argv) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    FILE *fp;

    if (argc < 2) {
        fprintf(stderr, "用法: %s <输出文件路径>\n", argv[0]);
        return 1;
    }

    if (bm_bus_open(&g_dump_in_bus, &dump_in_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_dump_fast_out_bus, &dump_fast_out_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_dump_mid_out_bus, &dump_mid_out_bus_storage, &cfg) != BM_OK ||
        bm_bus_open(&g_dump_slow_out_bus, &dump_slow_out_bus_storage, &cfg) != BM_OK) {
        fprintf(stderr, "bm_bus_open 失败\n");
        return 1;
    }

    if (bm_tt_schedule_init(&sched_dump) != BM_OK) {
        fprintf(stderr, "bm_tt_schedule_init 失败\n");
        return 1;
    }

    fp = fopen(argv[1], "w");
    if (fp == NULL) {
        fprintf(stderr, "fopen 失败: %s\n", argv[1]);
        return 1;
    }

    bm_tt_schedule_report(&sched_dump, dump_emit, fp);

    fclose(fp);
    return 0;
}

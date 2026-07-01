/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tt_schedule.c
 * @brief 时间触发调度门面（bm_tt_schedule）骨架实现
 *
 * 本文件当前仅为空壳：`bm_tt_schedule_init` 返回 `BM_OK`，其余 API 均为
 * 合理占位桩（返回 0 / 空 slot / BM_ERR_INVALID），不含任何算法逻辑。
 * 引擎实现（init 校验、ISR 派发、MAINLOOP pending 执行、RTA 导出）留待
 * 后续 Task 填充。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            骨架发布（config+公共头+CMake，无算法实现）
 *
 */
#include "bm_tt_schedule.h"
#include "bm_config.h"
#include "bm_log.h"

#include <string.h>

/**
 * @brief step 内读取输入快照（占位桩，暂不解析绑定表）
 */
const void *bm_let_in(bm_let_ctx_t *ctx, uint32_t in_idx, int *out_stale,
                       uint32_t *out_age_us) {
    (void)ctx;
    (void)in_idx;
    if (out_stale) {
        *out_stale = 0;
    }
    if (out_age_us) {
        *out_age_us = 0u;
    }
    return NULL;
}

/**
 * @brief step 内获取输出写入缓冲（占位桩）
 */
void *bm_let_out(bm_let_ctx_t *ctx, uint32_t out_idx) {
    (void)ctx;
    (void)out_idx;
    return NULL;
}

/**
 * @brief 初始化调度表（占位桩，暂不校验/分配布局）
 */
int bm_tt_schedule_init(bm_tt_schedule_t *sched) {
    (void)sched;
    return BM_OK;
}

/**
 * @brief 生成本调度表对应的 HRT slot 描述（占位桩，空 slot）
 */
bm_hrt_slot_t bm_tt_schedule_hrt_slot(bm_tt_schedule_t *sched) {
    bm_hrt_slot_t slot;

    (void)sched;
    memset(&slot, 0, sizeof(slot));
    return slot;
}

/**
 * @brief ISR 派发器（占位桩，暂不派发）
 */
void bm_tt_schedule_tick(bm_tt_schedule_t *sched) {
    (void)sched;
}

/**
 * @brief 主循环调用：跑 MAINLOOP 域 pending step（占位桩，暂不运行）
 */
uint32_t bm_tt_schedule_run_pending(bm_tt_schedule_t *sched, uint32_t budget) {
    (void)sched;
    (void)budget;
    return 0u;
}

/**
 * @brief 输出调度表可读诊断报告（占位桩，暂不输出）
 */
void bm_tt_schedule_report(const bm_tt_schedule_t *sched,
                           void (*emit)(const char *line, void *u), void *u) {
    (void)sched;
    (void)emit;
    (void)u;
}

/**
 * @brief 查询调度表可导出的 RTA slot 数量（占位桩，恒为 0）
 */
uint32_t bm_tt_schedule_rt_slot_count(const bm_tt_schedule_t *sched) {
    (void)sched;
    return 0u;
}

/**
 * @brief 按索引导出 RTA 中立只读描述符（占位桩，恒返回 BM_ERR_INVALID）
 */
int bm_tt_schedule_rt_slot_at(const bm_tt_schedule_t *sched, uint32_t idx,
                              bm_tt_schedule_rt_slot_t *out) {
    (void)sched;
    (void)idx;
    (void)out;
    return BM_ERR_INVALID;
}

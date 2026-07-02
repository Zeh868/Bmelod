/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_wcet_mon.c
 * @brief 运行时 deadline/WCET 监控（bm_wcet_mon）实现
 *
 * 本轮（Task 1）落实骨架：init 清态、register 定长指针表登记（重复/满表
 * 拒绝）、按注册顺序只读迭代、set_sink 绑定模块级快回调。Task 2 补上
 * begin/end 计时与预算超额判定、misuse 语义；report_miss 仍留空体占位，
 * 实现见 Task 3。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       1.0            zeh            Task 1：config/公共头/注册迭代骨架，
 *                                                 begin/end/report_miss 占位空体
 * 2026-07-02       1.1            zeh            Task 2：begin/end 计时、预算超额判定、
 *                                                 misuse 语义；report_miss 仍占位
 *
 */
#include "bm/hybrid/bm_wcet_mon.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_uptime.h"
#include "bm/common/bm_safety.h"
#include "bm_config.h"

#include <stddef.h>

/** 注册表：定长指针表，init 阶段填、运行期只读 */
static bm_wcet_span_t *s_spans[BM_CONFIG_WCET_MON_MAX_SPANS];
/** 已注册监控段个数 */
static uint32_t        s_span_count;
/** 模块级 sink 回调（fn/user 非原子对 → 仅限 init 阶段设置） */
static bm_wcet_sink_fn s_sink;
/** 模块级 sink 回调透传的用户上下文 */
static void           *s_sink_user;

/**
 * @brief 初始化 bm_wcet_mon 模块（清注册表与 sink 绑定）
 */
void bm_wcet_mon_init(void) {
    s_span_count = 0u;
    s_sink = NULL;
    s_sink_user = NULL;
    (void)bm_uptime_us(); /* 预热懒初始化，避免 ISR 内首调 */
}

/**
 * @brief 注册一个监控段（拒绝 NULL / 重复 / 满表）
 */
int bm_wcet_mon_register(bm_wcet_span_t *span) {
    if (span == NULL) {
        return BM_ERR_INVALID;
    }
    for (uint32_t i = 0u; i < s_span_count; ++i) {
        if (s_spans[i] == span) {
            return BM_ERR_ALREADY;
        }
    }
    if (s_span_count >= BM_CONFIG_WCET_MON_MAX_SPANS) {
        return BM_ERR_NO_MEM;
    }
    s_spans[s_span_count] = span;
    s_span_count++;
    return BM_OK;
}

/**
 * @brief 设置模块级 sink 快回调
 */
void bm_wcet_mon_set_sink(bm_wcet_sink_fn fn, void *user) {
    s_sink = fn;
    s_sink_user = user;
}

/**
 * @brief 查询已注册监控段数量
 */
uint32_t bm_wcet_mon_span_count(void) {
    return s_span_count;
}

/**
 * @brief 按注册顺序索引访问监控段（越界返回 NULL）
 */
const bm_wcet_span_t *bm_wcet_mon_span_at(uint32_t idx) {
    if (idx >= s_span_count) {
        return NULL;
    }
    return s_spans[idx];
}

/* report_miss：Task 3 实现；本任务先给空体占位保证可链接 */

/**
 * @brief 进入监控段：记录 t0 并置 running；已 running 再 begin 记 misuse 并覆盖 t0
 * @param span 监控段；NULL 直接返回
 */
void bm_wcet_mon_begin(bm_wcet_span_t *span) {
    if (span == NULL) {
        return;
    }
    if (span->running != 0u) {
        span->misuse_count = bm_u32_saturating_inc(span->misuse_count);
    }
    span->t0_us = bm_uptime_us();
    span->running = 1u;
}

/**
 * @brief 离开监控段：算 elapsed（64 位差钳 32 位）、更新观测面；超预算计数并调 sink
 * @param span 监控段；NULL 直接返回；未 running 仅记 misuse
 */
void bm_wcet_mon_end(bm_wcet_span_t *span) {
    uint64_t delta;
    uint32_t elapsed;

    if (span == NULL) {
        return;
    }
    if (span->running == 0u) {
        span->misuse_count = bm_u32_saturating_inc(span->misuse_count);
        return;
    }
    delta = bm_uptime_us() - span->t0_us;
    elapsed = (delta > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)delta;
    span->running = 0u;
    span->last_us = elapsed;
    if (elapsed > span->max_us) {
        span->max_us = elapsed;
    }
    span->run_count = bm_u32_saturating_inc(span->run_count);
    if (span->budget_us > 0u && elapsed > span->budget_us) {
        span->overrun_count = bm_u32_saturating_inc(span->overrun_count);
        if (s_sink != NULL) {
            s_sink(span, BM_WCET_EVT_BUDGET_OVERRUN, elapsed, s_sink_user);
        }
    }
}

/**
 * @brief 显式上报 deadline miss（占位空体，实现见 Task 3）
 */
void bm_wcet_mon_report_miss(bm_wcet_span_t *span) { (void)span; }

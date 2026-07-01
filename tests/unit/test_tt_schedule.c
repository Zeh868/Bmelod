/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_tt_schedule.c
 * @brief bm_tt_schedule 单元测试：输入冻结 + seq-delta 判龄（Task 3）
 *
 * 覆盖两个场景：
 *   1. 输入 bus 从未发布 → 冻结失败 → stale=1（safe_default 兜底路径）。
 *   2. 慢生产者（每 3 拍发一次）被每拍消费的任务冻结 → miss 在 0..2 摆动、
 *      age = miss × 任务周期，且不越过默认保质期（2×周期）不置 stale。
 *
 * 装配全部经公共头宏（BM_BUS_DEFINE/BM_LET_DEFINE/BM_SCHEDULE_DEFINE），
 * 走门面 API（bm_tt_schedule_tick/bm_let_in）+ 公共 bus API
 * （bm_bus_open/acquire_write/commit），不需要 BM_BUS_ALLOW_INTERNAL。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            Task 3：输入冻结 + seq-delta 判龄测试
 *
 */
#include "unity.h"
#include "bm_tt_schedule.h"
#include "bm_bus.h"

#include <string.h>

/* =========================================================================
 * 场景 1：从未发布的输入 bus
 * ========================================================================= */

BM_BUS_DEFINE(never_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(never_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_never_bus;
static bm_bus_t g_never_out_bus;

static const uint32_t k_never_in_safe = 0xDEADBEEFu;
static const uint32_t k_never_out_safe = 0u;

static int      g_never_stale;
static uint32_t g_never_age;
static uint32_t g_never_snapshot_val; /**< 记录冻结快照实际写入值，用于断言 safe_default 兜底 */

/** @brief 只读输入、记录 stale/age，不写输出（bm_let_out 是 Task4 桩，本任务不调） */
static void never_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const void *p;

    (void)state;
    p = bm_let_in(ctx, 0u, &stale, &age);
    g_never_stale = stale;
    g_never_age = age;
    g_never_snapshot_val = *(const uint32_t *)p;
}

static const bm_let_input_t k_never_inputs[] = {
    { .bus = &g_never_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_never_in_safe },
};
static const bm_let_output_t k_never_outputs[] = {
    { .bus = &g_never_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_never_out_safe },
};

BM_LET_DEFINE(task_never, 1u, 0u, 100u, never_step, NULL,
              k_never_inputs, k_never_outputs);
BM_SCHEDULE_DEFINE(sched_never, 1000u, &task_never);

/* =========================================================================
 * 场景 2：慢生产者（每 3 拍发一次）+ 每拍消费
 * ========================================================================= */

BM_BUS_DEFINE(slow_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(slow_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_slow_bus;
static bm_bus_t g_slow_out_bus;

static const uint32_t k_slow_in_safe = 0u;
static const uint32_t k_slow_out_safe = 0u;

static int      g_slow_stale;
static uint32_t g_slow_age;

static void slow_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;

    (void)state;
    (void)bm_let_in(ctx, 0u, &stale, &age);
    g_slow_stale = stale;
    g_slow_age = age;
}

static const bm_let_input_t k_slow_inputs[] = {
    { .bus = &g_slow_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_slow_in_safe },
};
static const bm_let_output_t k_slow_outputs[] = {
    { .bus = &g_slow_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_slow_out_safe },
};

BM_LET_DEFINE(task_slow, 1u, 0u, 100u, slow_step, NULL,
              k_slow_inputs, k_slow_outputs);
BM_SCHEDULE_DEFINE(sched_slow, 1000u, &task_slow);

/** @brief 小发布助手：acquire_write → memcpy → commit（LATEST 无单调用 write API） */
static int publish_u32(bm_bus_t *h, uint32_t v) {
    void *slot;
    int rc = bm_bus_acquire_write(h, &slot);

    if (rc != BM_OK) {
        return rc;
    }
    (void)memcpy(slot, &v, sizeof v);
    return bm_bus_commit(h);
}

void setUp(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_never_bus, &never_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_never_out_bus, &never_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_slow_bus, &slow_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_slow_out_bus, &slow_out_bus_storage, &cfg));

    /* 本任务不实现 init：显式给单任务表 n_frames，跳过 LCM 兜底路径 */
    sched_never.n_frames = 1u;
    sched_never.tick_idx = 0u;
    sched_slow.n_frames = 1u;
    sched_slow.tick_idx = 0u;

    g_never_stale = 0; g_never_age = 0u; g_never_snapshot_val = 0u;
    g_slow_stale = 0; g_slow_age = 0u;
}

void tearDown(void) {
    bm_bus_close(&g_never_bus);
    bm_bus_close(&g_never_out_bus);
    bm_bus_close(&g_slow_bus);
    bm_bus_close(&g_slow_out_bus);
}

/**
 * @brief 输入 bus 从未发布：冻结走 safe_default 兜底路径，stale 恒为 1。
 */
void test_freeze_stale_when_never_published(void) {
    bm_tt_schedule_tick(&sched_never);
    TEST_ASSERT_EQUAL(1, g_never_stale);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, g_never_age);
    TEST_ASSERT_EQUAL_UINT32(k_never_in_safe, g_never_snapshot_val);
}

/**
 * @brief 慢生产者每 3 拍发一次、消费者每拍读：miss 在 0..2 摆动，
 * age = miss × 任务周期（1000us），且不超过默认保质期（2×周期=2000us）不置 stale。
 */
void test_freeze_seq_delta_slow_producer(void) {
    uint32_t v = 1u;

    for (uint32_t i = 0u; i < 9u; ++i) {
        uint32_t expect_miss;
        uint32_t expect_age;

        if ((i % 3u) == 0u) {
            TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_slow_bus, v));
            ++v;
        }
        bm_tt_schedule_tick(&sched_slow);

        expect_miss = i % 3u;
        expect_age = expect_miss * 1000u;
        TEST_ASSERT_EQUAL_UINT32(expect_age, g_slow_age);
        TEST_ASSERT_EQUAL(0, g_slow_stale);
    }
}

/* =========================================================================
 * 场景 3：偶数分频任务 per-task 相位双缓冲边界发布（Task 4）
 *   every=2,at=0：命中拍 tick_idx 恒为偶数——若误用 tick_idx&1 当 phase
 *   （旧 tick&1 回归 bug），phase 会永远停在同一份缓冲，输出永远不翻转。
 * ========================================================================= */

BM_BUS_DEFINE(phase_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(phase_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_phase_in_bus;
static bm_bus_t g_phase_out_bus;

static const uint32_t k_phase_in_safe = 0u;
static const uint32_t k_phase_out_safe = 0u;

/** @brief 原样把输入值写入输出槽，便于区分"这次算出的值"与"上次算出的值" */
static void phase_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const uint32_t *in;
    uint32_t *out;

    (void)state;
    in = (const uint32_t *)bm_let_in(ctx, 0u, &stale, &age);
    out = (uint32_t *)bm_let_out(ctx, 0u);
    *out = *in;
}

static const bm_let_input_t k_phase_inputs[] = {
    { .bus = &g_phase_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_phase_in_safe },
};
static const bm_let_output_t k_phase_outputs[] = {
    { .bus = &g_phase_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_phase_out_safe },
};

BM_LET_DEFINE(task_phase, 2u, 0u, 100u, phase_step, NULL,
              k_phase_inputs, k_phase_outputs);
BM_SCHEDULE_DEFINE(sched_phase, 1000u, &task_phase);

/**
 * @brief 偶数分频任务连续 4 次命中：断言发布值 = 上一次命中 step 算出的结果，
 * 且跨命中确实在轮换（防 phase 永远停在同一份缓冲的回归）。
 */
void test_phase_double_buffer_boundary_publish(void) {
    uint32_t out_val;
    uint32_t inputs[4] = { 11u, 22u, 33u, 44u };
    uint32_t hit = 0u;

    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_phase_in_bus, &phase_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_phase_out_bus, &phase_out_bus_storage, &cfg));

    /* 本任务不实现 init：手动给 n_frames，使 tick_idx 按 0,1,0,1... 循环，
     * 命中拍（tick_idx%2==0）恒为偶数 tick_idx，用于暴露 tick&1 类回归。 */
    sched_phase.n_frames = 2u;
    sched_phase.tick_idx = 0u;

    for (uint32_t tick_call = 0u; tick_call < 8u; ++tick_call) {
        int is_hit = ((sched_phase.tick_idx % task_phase.every) == task_phase.at);

        if (is_hit) {
            TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_phase_in_bus, inputs[hit]));
        }
        bm_tt_schedule_tick(&sched_phase);

        if (is_hit) {
            int rc = bm_bus_latest_read(&g_phase_out_bus, &out_val);
            TEST_ASSERT_EQUAL(BM_OK, rc);
            if (hit == 0u) {
                /* 第一次命中：发布的是 outbuf 初值（宏静态分配为 0） */
                TEST_ASSERT_EQUAL_UINT32(0u, out_val);
            } else {
                /* 第 N 次命中：发布的是第 N-1 次 step 算出的结果（+1 拍延迟） */
                TEST_ASSERT_EQUAL_UINT32(inputs[hit - 1u], out_val);
            }
            ++hit;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(4u, hit);

    bm_bus_close(&g_phase_in_bus);
    bm_bus_close(&g_phase_out_bus);
}

/* =========================================================================
 * 场景 4：ISR reentry guard / overrun（Task 5）
 * ========================================================================= */

BM_BUS_DEFINE(reentry_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(reentry_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_reentry_in_bus;
static bm_bus_t g_reentry_out_bus;

static const uint32_t k_reentry_in_safe = 0u;
static const uint32_t k_reentry_out_safe = 0u;

static uint32_t g_reentry_step_calls;

/** @brief 原样把输入值写入输出槽，并计数 step 实际执行次数 */
static void reentry_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const uint32_t *in;
    uint32_t *out;

    (void)state;
    in = (const uint32_t *)bm_let_in(ctx, 0u, &stale, &age);
    out = (uint32_t *)bm_let_out(ctx, 0u);
    *out = *in;
    ++g_reentry_step_calls;
}

static const bm_let_input_t k_reentry_inputs[] = {
    { .bus = &g_reentry_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_reentry_in_safe },
};
static const bm_let_output_t k_reentry_outputs[] = {
    { .bus = &g_reentry_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_reentry_out_safe },
};

BM_LET_DEFINE(task_reentry, 1u, 0u, 100u, reentry_step, NULL,
              k_reentry_inputs, k_reentry_outputs);
BM_SCHEDULE_DEFINE(sched_reentry, 1000u, &task_reentry);

/**
 * @brief ISR reentry guard：running 已为真时本拍整体跳过（不 freeze/不
 * step/不 publish），仅 overrun_count+1；下游 bus 值与 seq 保持不变。
 */
void test_isr_reentry_guard_skips_and_overruns(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t seq_before;
    uint32_t seq_after;
    uint32_t val_before;
    uint32_t val_after;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_reentry_in_bus, &reentry_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_reentry_out_bus, &reentry_out_bus_storage, &cfg));

    sched_reentry.n_frames = 1u;
    sched_reentry.tick_idx = 0u;
    task_reentry.rt->overrun_count = 0u;
    task_reentry.rt->running = 0u;
    g_reentry_step_calls = 0u;

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_reentry_in_bus, 7u));
    bm_tt_schedule_tick(&sched_reentry); /* 正常一拍：running 清零，产出可观测基线 */
    TEST_ASSERT_EQUAL_UINT32(1u, g_reentry_step_calls);
    TEST_ASSERT_EQUAL_UINT32(0u, task_reentry.rt->overrun_count);

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&g_reentry_out_bus, &val_before, &seq_before));

    task_reentry.rt->running = 1u; /* 模拟上一拍未清 running：本拍应被跳过 */
    bm_tt_schedule_tick(&sched_reentry);

    TEST_ASSERT_EQUAL_UINT32(1u, task_reentry.rt->overrun_count);
    TEST_ASSERT_EQUAL_UINT32(1u, g_reentry_step_calls); /* step 未再被调用 */

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&g_reentry_out_bus, &val_after, &seq_after));
    TEST_ASSERT_EQUAL_UINT32(seq_before, seq_after);
    TEST_ASSERT_EQUAL_UINT32(val_before, val_after);

    bm_bus_close(&g_reentry_in_bus);
    bm_bus_close(&g_reentry_out_bus);
}

/* =========================================================================
 * 场景 5：MAINLOOP 冻结挂起 + run_pending +1 拍语义（Task 5）
 * ========================================================================= */

BM_BUS_DEFINE(mainloop_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(mainloop_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_mainloop_in_bus;
static bm_bus_t g_mainloop_out_bus;

static const uint32_t k_mainloop_in_safe = 0u;
static const uint32_t k_mainloop_out_safe = 0u;

static uint32_t g_mainloop_step_calls;

static void mainloop_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const uint32_t *in;
    uint32_t *out;

    (void)state;
    in = (const uint32_t *)bm_let_in(ctx, 0u, &stale, &age);
    out = (uint32_t *)bm_let_out(ctx, 0u);
    *out = *in;
    ++g_mainloop_step_calls;
}

static const bm_let_input_t k_mainloop_inputs[] = {
    { .bus = &g_mainloop_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_mainloop_in_safe },
};
static const bm_let_output_t k_mainloop_outputs[] = {
    { .bus = &g_mainloop_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_mainloop_out_safe },
};

BM_LET_DEFINE(task_mainloop, 1u, 0u, 100u, mainloop_step, NULL,
              k_mainloop_inputs, k_mainloop_outputs);
BM_SCHEDULE_DEFINE(sched_mainloop, 1000u, &task_mainloop);

/**
 * @brief MAINLOOP 域 +1 拍语义：ISR 冻结在先、run_pending 里 step 在后，
 * 结果要等下一次命中 tick 才发布（fresh 标记驱动）。
 */
void test_mainloop_freeze_then_run_pending_then_publish_next_tick(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t out_val;
    uint32_t ran;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mainloop_in_bus, &mainloop_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mainloop_out_bus, &mainloop_out_bus_storage, &cfg));

    task_mainloop.domain = BM_TT_DOMAIN_MAINLOOP;
    sched_mainloop.n_frames = 1u;
    sched_mainloop.tick_idx = 0u;
    task_mainloop.rt->pending = 0u;
    task_mainloop.rt->fresh = 0u;
    task_mainloop.rt->overrun_count = 0u;
    g_mainloop_step_calls = 0u;

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mainloop_in_bus, 42u));

    bm_tt_schedule_tick(&sched_mainloop); /* 第 1 拍：只冻结、置 pending，不跑 step */
    TEST_ASSERT_EQUAL_UINT32(0u, g_mainloop_step_calls);
    TEST_ASSERT_EQUAL(1, task_mainloop.rt->pending);
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_bus_latest_read(&g_mainloop_out_bus, &out_val));

    ran = bm_tt_schedule_run_pending(&sched_mainloop, 8u);
    TEST_ASSERT_EQUAL_UINT32(1u, ran);
    TEST_ASSERT_EQUAL_UINT32(1u, g_mainloop_step_calls);
    TEST_ASSERT_EQUAL(0, task_mainloop.rt->pending);
    TEST_ASSERT_EQUAL(1, task_mainloop.rt->fresh);
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_bus_latest_read(&g_mainloop_out_bus, &out_val)); /* run_pending 本身不发布 */

    bm_tt_schedule_tick(&sched_mainloop); /* 第 2 拍：先发布上一拍结果，再重新冻结挂起 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_mainloop_out_bus, &out_val));
    TEST_ASSERT_EQUAL_UINT32(42u, out_val);
    TEST_ASSERT_EQUAL(0, task_mainloop.rt->fresh);
    TEST_ASSERT_EQUAL(1, task_mainloop.rt->pending);
    TEST_ASSERT_EQUAL_UINT32(0u, task_mainloop.rt->overrun_count);

    bm_bus_close(&g_mainloop_in_bus);
    bm_bus_close(&g_mainloop_out_bus);
}

/* =========================================================================
 * 场景 6：MAINLOOP 主循环 overrun（Task 5）
 * ========================================================================= */

BM_BUS_DEFINE(mainloop2_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(mainloop2_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_mainloop2_in_bus;
static bm_bus_t g_mainloop2_out_bus;

static const uint32_t k_mainloop2_in_safe = 0u;
static const uint32_t k_mainloop2_out_safe = 0u;

static void mainloop2_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const uint32_t *in;
    uint32_t *out;

    (void)state;
    in = (const uint32_t *)bm_let_in(ctx, 0u, &stale, &age);
    out = (uint32_t *)bm_let_out(ctx, 0u);
    *out = *in;
}

static const bm_let_input_t k_mainloop2_inputs[] = {
    { .bus = &g_mainloop2_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_mainloop2_in_safe },
};
static const bm_let_output_t k_mainloop2_outputs[] = {
    { .bus = &g_mainloop2_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_mainloop2_out_safe },
};

BM_LET_DEFINE(task_mainloop2, 1u, 0u, 100u, mainloop2_step, NULL,
              k_mainloop2_inputs, k_mainloop2_outputs);
BM_SCHEDULE_DEFINE(sched_mainloop2, 1000u, &task_mainloop2);

/**
 * @brief MAINLOOP 主循环 overrun：连续两拍未调用 run_pending，第二拍命中时
 * overrun_count+1，且已冻结的输入快照不被第二次冻结覆盖。
 */
void test_mainloop_overrun_when_run_pending_not_called(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t snap_val;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mainloop2_in_bus, &mainloop2_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mainloop2_out_bus, &mainloop2_out_bus_storage, &cfg));

    task_mainloop2.domain = BM_TT_DOMAIN_MAINLOOP;
    sched_mainloop2.n_frames = 1u;
    sched_mainloop2.tick_idx = 0u;
    task_mainloop2.rt->pending = 0u;
    task_mainloop2.rt->fresh = 0u;
    task_mainloop2.rt->overrun_count = 0u;

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mainloop2_in_bus, 100u));
    bm_tt_schedule_tick(&sched_mainloop2); /* 第 1 拍：冻结 100，置 pending */
    TEST_ASSERT_EQUAL(1, task_mainloop2.rt->pending);

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mainloop2_in_bus, 200u)); /* 主循环还没消化，输入源已更新 */
    bm_tt_schedule_tick(&sched_mainloop2); /* 第 2 拍：未调用 run_pending → overrun */

    TEST_ASSERT_EQUAL_UINT32(1u, task_mainloop2.rt->overrun_count);
    TEST_ASSERT_EQUAL(1, task_mainloop2.rt->pending); /* 仍挂起，未被覆盖式重冻结 */

    snap_val = *(const uint32_t *)task_mainloop2.snapshot;
    TEST_ASSERT_EQUAL_UINT32(100u, snap_val); /* 快照仍是第一次冻结时的值，未被 200 覆盖 */

    bm_bus_close(&g_mainloop2_in_bus);
    bm_bus_close(&g_mainloop2_out_bus);
}

/* =========================================================================
 * 场景 7：ISR 分频 / 错峰（Task 5）
 *   balance(every=1,at=0)/estimator(every=5,at=0)/telemetry(every=10,at=9)
 *   跑 20 拍，验证命中次数与命中拍序号 (tick_idx % every)==at。
 * ========================================================================= */

BM_BUS_DEFINE(freq_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(freq_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_freq_in_bus;
static bm_bus_t g_freq_out_bus;

static const uint32_t k_freq_in_safe = 0u;
static const uint32_t k_freq_out_safe = 0u;

static uint8_t g_balance_hit;
static uint8_t g_estimator_hit;
static uint8_t g_telemetry_hit;

static void balance_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
    g_balance_hit = 1u;
}
static void estimator_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
    g_estimator_hit = 1u;
}
static void telemetry_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
    g_telemetry_hit = 1u;
}

static const bm_let_input_t k_freq_inputs[] = {
    { .bus = &g_freq_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_freq_in_safe },
};
static const bm_let_output_t k_freq_outputs[] = {
    { .bus = &g_freq_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_freq_out_safe },
};

BM_LET_DEFINE(task_balance, 1u, 0u, 100u, balance_step, NULL, k_freq_inputs, k_freq_outputs);
BM_LET_DEFINE(task_estimator, 5u, 0u, 100u, estimator_step, NULL, k_freq_inputs, k_freq_outputs);
BM_LET_DEFINE(task_telemetry, 10u, 9u, 100u, telemetry_step, NULL, k_freq_inputs, k_freq_outputs);
BM_SCHEDULE_DEFINE(sched_freq, 1000u, &task_balance, &task_estimator, &task_telemetry);

/**
 * @brief 三个 ISR 域任务分频/错峰跑 20 拍：命中次数分别为 20/4/2，
 * 命中拍序号满足 (tick_idx % every)==at。
 */
void test_isr_frequency_division_and_phase_offset(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t balance_hits = 0u;
    uint32_t estimator_hits = 0u;
    uint32_t telemetry_hits = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_freq_in_bus, &freq_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_freq_out_bus, &freq_out_bus_storage, &cfg));

    sched_freq.n_frames = 20u;
    sched_freq.tick_idx = 0u;

    for (uint32_t i = 0u; i < 20u; ++i) {
        g_balance_hit = 0u;
        g_estimator_hit = 0u;
        g_telemetry_hit = 0u;

        bm_tt_schedule_tick(&sched_freq);

        if (g_balance_hit) {
            TEST_ASSERT_EQUAL_UINT32(0u, i % 1u);
            ++balance_hits;
        }
        if (g_estimator_hit) {
            TEST_ASSERT_EQUAL_UINT32(0u, i % 5u);
            ++estimator_hits;
        }
        if (g_telemetry_hit) {
            TEST_ASSERT_EQUAL_UINT32(9u, i % 10u);
            ++telemetry_hits;
        }
    }

    TEST_ASSERT_EQUAL_UINT32(20u, balance_hits);
    TEST_ASSERT_EQUAL_UINT32(4u, estimator_hits);
    TEST_ASSERT_EQUAL_UINT32(2u, telemetry_hits);

    bm_bus_close(&g_freq_in_bus);
    bm_bus_close(&g_freq_out_bus);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_freeze_stale_when_never_published);
    RUN_TEST(test_freeze_seq_delta_slow_producer);
    RUN_TEST(test_phase_double_buffer_boundary_publish);
    RUN_TEST(test_isr_reentry_guard_skips_and_overruns);
    RUN_TEST(test_mainloop_freeze_then_run_pending_then_publish_next_tick);
    RUN_TEST(test_mainloop_overrun_when_run_pending_not_called);
    RUN_TEST(test_isr_frequency_division_and_phase_offset);
    return UNITY_END();
}

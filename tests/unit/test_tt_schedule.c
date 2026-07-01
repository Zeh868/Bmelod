/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_tt_schedule.c
 * @brief bm_tt_schedule 单元测试：输入冻结/双缓冲发布/双域派发/init（Task 3-6）
 *
 * 场景 8-11（Task 6）覆盖真 `bm_tt_schedule_init`：
 *   8. A2① 节拍过载负样本——某 minor 格内 ISR 域 wcet 之和超载 → BM_ERR_INVALID。
 *   9. A2② LCM 爆炸负样本——互质 every 使 N=LCM(every) 超 MAX_FRAMES → BM_ERR_INVALID。
 *  10. init 预发布——init 后、任何 tick 之前，下游 bus 立即可读到 safe_default，rt 复位。
 *  11. 谐波周期正样本——1/5/10ms（minor=1ms）init 返回 BM_OK，n_frames=LCM=10，
 *      跑通一条数据流。
 *
 * 装配全部经公共头宏（BM_BUS_DEFINE/BM_LET_DEFINE/BM_SCHEDULE_DEFINE），
 * 走门面 API（bm_tt_schedule_tick/bm_let_in）+ 公共 bus API
 * （bm_bus_open/acquire_write/commit），不需要 BM_BUS_ALLOW_INTERNAL。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       1.0            zeh            Task 3：输入冻结 + seq-delta 判龄测试
 * 2026-07-01       1.1            zeh            Task 6：真 init 测试（A2 负样本×2 +
 *                                                 预发布 + 谐波正样本）
 * 2026-07-01       1.2            zeh            Task 7：rt_slot_count/at 导出测试 +
 *                                                 report 报告文本断言（表头子串 + 峰值格标记）
 * 2026-07-01       1.3            zeh            Task 8：验收收口——A1 低频双缓冲 +
 *                                                 A1-MAINLOOP 同表混跑/饿死可见 + A4①~④ +
 *                                                 A3-lite 端到端数据流 + A5 确定性不变量回归锁
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

/* =========================================================================
 * 场景 8：init 真实现——A2① 节拍过载负样本（Task 6）
 *   两个 ISR 任务 every=1/at=0（每拍都命中同一格），wcet 各 600us，
 *   minor=1000us：Σwcet=1200>1000 快路不过，慢路逐格累加同样在 t=0 溢出。
 * ========================================================================= */

BM_BUS_DEFINE(overload_a_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(overload_a_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(overload_b_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(overload_b_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_overload_a_in_bus;
static bm_bus_t g_overload_a_out_bus;
static bm_bus_t g_overload_b_in_bus;
static bm_bus_t g_overload_b_out_bus;

static const uint32_t k_overload_in_safe = 0u;
static const uint32_t k_overload_out_safe = 0u;

/** @brief 空 step，本场景只关心 init 的节拍负载校验，不关心数据流 */
static void overload_noop_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_overload_a_inputs[] = {
    { .bus = &g_overload_a_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_overload_in_safe },
};
static const bm_let_output_t k_overload_a_outputs[] = {
    { .bus = &g_overload_a_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_overload_out_safe },
};
static const bm_let_input_t k_overload_b_inputs[] = {
    { .bus = &g_overload_b_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_overload_in_safe },
};
static const bm_let_output_t k_overload_b_outputs[] = {
    { .bus = &g_overload_b_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_overload_out_safe },
};

BM_LET_DEFINE(task_overload_a, 1u, 0u, 600u, overload_noop_step, NULL,
              k_overload_a_inputs, k_overload_a_outputs);
BM_LET_DEFINE(task_overload_b, 1u, 0u, 600u, overload_noop_step, NULL,
              k_overload_b_inputs, k_overload_b_outputs);
BM_SCHEDULE_DEFINE(sched_overload, 1000u, &task_overload_a, &task_overload_b);

/**
 * @brief 某 minor 格内 ISR 域任务 wcet 之和超过 minor_us：init 拒绝。
 */
void test_init_rejects_frame_overload(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_overload_a_in_bus, &overload_a_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_overload_a_out_bus, &overload_a_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_overload_b_in_bus, &overload_b_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_overload_b_out_bus, &overload_b_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tt_schedule_init(&sched_overload));

    bm_bus_close(&g_overload_a_in_bus);
    bm_bus_close(&g_overload_a_out_bus);
    bm_bus_close(&g_overload_b_in_bus);
    bm_bus_close(&g_overload_b_out_bus);
}

/* =========================================================================
 * 场景 9：init 真实现——A2② LCM 爆炸负样本（Task 6）
 *   every 互质（17/19）：LCM=323 > BM_CONFIG_TT_SCHED_MAX_FRAMES(256)。
 * ========================================================================= */

BM_BUS_DEFINE(lcm_a_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(lcm_a_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(lcm_b_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(lcm_b_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_lcm_a_in_bus;
static bm_bus_t g_lcm_a_out_bus;
static bm_bus_t g_lcm_b_in_bus;
static bm_bus_t g_lcm_b_out_bus;

static const uint32_t k_lcm_in_safe = 0u;
static const uint32_t k_lcm_out_safe = 0u;

static const bm_let_input_t k_lcm_a_inputs[] = {
    { .bus = &g_lcm_a_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_lcm_in_safe },
};
static const bm_let_output_t k_lcm_a_outputs[] = {
    { .bus = &g_lcm_a_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_lcm_out_safe },
};
static const bm_let_input_t k_lcm_b_inputs[] = {
    { .bus = &g_lcm_b_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_lcm_in_safe },
};
static const bm_let_output_t k_lcm_b_outputs[] = {
    { .bus = &g_lcm_b_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_lcm_out_safe },
};

BM_LET_DEFINE(task_lcm_a, 17u, 0u, 50u, overload_noop_step, NULL,
              k_lcm_a_inputs, k_lcm_a_outputs);
BM_LET_DEFINE(task_lcm_b, 19u, 0u, 50u, overload_noop_step, NULL,
              k_lcm_b_inputs, k_lcm_b_outputs);
BM_SCHEDULE_DEFINE(sched_lcm_explosion, 1000u, &task_lcm_a, &task_lcm_b);

/**
 * @brief 互质 every 使 N=LCM(every) 超过 MAX_FRAMES：init 拒绝（挡表爆炸）。
 */
void test_init_rejects_lcm_explosion(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_lcm_a_in_bus, &lcm_a_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_lcm_a_out_bus, &lcm_a_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_lcm_b_in_bus, &lcm_b_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_lcm_b_out_bus, &lcm_b_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tt_schedule_init(&sched_lcm_explosion));

    bm_bus_close(&g_lcm_a_in_bus);
    bm_bus_close(&g_lcm_a_out_bus);
    bm_bus_close(&g_lcm_b_in_bus);
    bm_bus_close(&g_lcm_b_out_bus);
}

/* =========================================================================
 * 场景 10：init 预发布 safe_default（Task 6）
 *   init 成功后、任何 tick 之前，下游 bus 立即能读到 safe_default；
 *   rt 全部复位为初始态（首拍安全值语义）。
 * ========================================================================= */

BM_BUS_DEFINE(prepub_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(prepub_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_prepub_in_bus;
static bm_bus_t g_prepub_out_bus;

static const uint32_t k_prepub_in_safe = 0xAAu;
static const uint32_t k_prepub_out_safe = 0x55u;

static void prepub_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_prepub_inputs[] = {
    { .bus = &g_prepub_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_prepub_in_safe },
};
static const bm_let_output_t k_prepub_outputs[] = {
    { .bus = &g_prepub_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_prepub_out_safe },
};

BM_LET_DEFINE(task_prepub, 1u, 0u, 100u, prepub_step, NULL,
              k_prepub_inputs, k_prepub_outputs);
BM_SCHEDULE_DEFINE(sched_prepub, 1000u, &task_prepub);

/**
 * @brief init 后立即可读到 safe_default；rt 复位为初始态；n_frames=LCM(every)=1。
 */
void test_init_pre_publishes_safe_default_before_any_tick(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t out_val;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_prepub_in_bus, &prepub_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_prepub_out_bus, &prepub_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_prepub));
    TEST_ASSERT_EQUAL_UINT32(1u, sched_prepub.n_frames);
    TEST_ASSERT_EQUAL_UINT32(0u, sched_prepub.tick_idx);

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_prepub_out_bus, &out_val));
    TEST_ASSERT_EQUAL_UINT32(k_prepub_out_safe, out_val);

    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->phase);
    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->running);
    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->pending);
    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->fresh);
    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->overrun_count);
    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->baseline_seq[0]); /* 输入从未发布 */
    TEST_ASSERT_EQUAL_UINT32(0u, task_prepub.rt->miss[0]);

    bm_bus_close(&g_prepub_in_bus);
    bm_bus_close(&g_prepub_out_bus);
}

/* =========================================================================
 * 场景 11：init 正样本——谐波周期（Task 6）
 *   1/5/10ms（minor=1ms）：N=LCM(1,5,10)=10；init 返回 BM_OK 后可正常
 *   tick 跑通一条数据流（fast 任务的输出滞后 1 拍等于上次输入）。
 * ========================================================================= */

BM_BUS_DEFINE(harm_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(harm_fast_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(harm_mid_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(harm_slow_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_harm_in_bus;
static bm_bus_t g_harm_fast_out_bus;
static bm_bus_t g_harm_mid_out_bus;
static bm_bus_t g_harm_slow_out_bus;

static const uint32_t k_harm_in_safe = 0u;
static const uint32_t k_harm_out_safe = 0u;

static uint8_t g_harm_fast_hit;
static uint8_t g_harm_mid_hit;
static uint8_t g_harm_slow_hit;

/** @brief 原样把输入值写入输出槽，兼计命中标志，验证真 init 后数据流跑通 */
static void harm_fast_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const uint32_t *in;
    uint32_t *out;

    (void)state;
    in = (const uint32_t *)bm_let_in(ctx, 0u, &stale, &age);
    out = (uint32_t *)bm_let_out(ctx, 0u);
    *out = *in;
    g_harm_fast_hit = 1u;
}
static void harm_mid_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
    g_harm_mid_hit = 1u;
}
static void harm_slow_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
    g_harm_slow_hit = 1u;
}

static const bm_let_input_t k_harm_inputs[] = {
    { .bus = &g_harm_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_harm_in_safe },
};
static const bm_let_output_t k_harm_fast_outputs[] = {
    { .bus = &g_harm_fast_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_harm_out_safe },
};
static const bm_let_output_t k_harm_mid_outputs[] = {
    { .bus = &g_harm_mid_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_harm_out_safe },
};
static const bm_let_output_t k_harm_slow_outputs[] = {
    { .bus = &g_harm_slow_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_harm_out_safe },
};

BM_LET_DEFINE(task_harm_fast, 1u, 0u, 50u, harm_fast_step, NULL, k_harm_inputs, k_harm_fast_outputs);
BM_LET_DEFINE(task_harm_mid, 5u, 0u, 50u, harm_mid_step, NULL, k_harm_inputs, k_harm_mid_outputs);
BM_LET_DEFINE(task_harm_slow, 10u, 9u, 50u, harm_slow_step, NULL, k_harm_inputs, k_harm_slow_outputs);
BM_SCHEDULE_DEFINE(sched_harm, 1000u, &task_harm_fast, &task_harm_mid, &task_harm_slow);

/**
 * @brief 谐波周期正样本：init 返回 BM_OK、n_frames=LCM(1,5,10)=10，
 * 命中次数分别为 10/2/1，且数据流跑通（+1 拍延迟语义与场景 3 一致）。
 */
void test_init_harmonic_periods_ok_and_runs_data_flow(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t out_val;
    uint32_t fast_hits = 0u;
    uint32_t mid_hits = 0u;
    uint32_t slow_hits = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_harm_in_bus, &harm_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_harm_fast_out_bus, &harm_fast_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_harm_mid_out_bus, &harm_mid_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_harm_slow_out_bus, &harm_slow_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_harm));
    TEST_ASSERT_EQUAL_UINT32(10u, sched_harm.n_frames);

    for (uint32_t i = 0u; i < 10u; ++i) {
        g_harm_fast_hit = 0u;
        g_harm_mid_hit = 0u;
        g_harm_slow_hit = 0u;

        TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_harm_in_bus, 100u + i));
        bm_tt_schedule_tick(&sched_harm);

        if (g_harm_fast_hit) {
            ++fast_hits;
        }
        if (g_harm_mid_hit) {
            ++mid_hits;
        }
        if (g_harm_slow_hit) {
            ++slow_hits;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(10u, fast_hits);
    TEST_ASSERT_EQUAL_UINT32(2u, mid_hits);
    TEST_ASSERT_EQUAL_UINT32(1u, slow_hits);

    /* 数据流跑通：最后一次 tick(i=9) 发布的是 i=8 那次算出的结果（+1 拍延迟） */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_harm_fast_out_bus, &out_val));
    TEST_ASSERT_EQUAL_UINT32(100u + 8u, out_val);

    bm_bus_close(&g_harm_in_bus);
    bm_bus_close(&g_harm_fast_out_bus);
    bm_bus_close(&g_harm_mid_out_bus);
    bm_bus_close(&g_harm_slow_out_bus);
}

/* =========================================================================
 * 场景 12：rt_slot 导出 + report 报告（Task 7）
 *   rt_slot_count/at 复用场景 7 的 sched_freq（三个 ISR 域任务，无需
 *   init——rt_slot 只读 entries 静态字段）；report 另建混合 ISR/MAINLOOP
 *   域小 fixture，专供"两块都出"的断言。
 * ========================================================================= */

BM_BUS_DEFINE(report_isr_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(report_isr_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(report_ml_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(report_ml_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_report_isr_in_bus;
static bm_bus_t g_report_isr_out_bus;
static bm_bus_t g_report_ml_in_bus;
static bm_bus_t g_report_ml_out_bus;

static const uint32_t k_report_in_safe = 0u;
static const uint32_t k_report_out_safe = 0u;

/** @brief 空 step：report 测试只关心报告文本，不关心真实数据流 */
static void report_isr_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}
static void report_ml_step(bm_let_ctx_t *ctx, void *state) {
    (void)ctx;
    (void)state;
}

static const bm_let_input_t k_report_isr_inputs[] = {
    { .bus = &g_report_isr_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_report_in_safe },
};
static const bm_let_output_t k_report_isr_outputs[] = {
    { .bus = &g_report_isr_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_report_out_safe },
};
static const bm_let_input_t k_report_ml_inputs[] = {
    { .bus = &g_report_ml_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_report_in_safe },
};
static const bm_let_output_t k_report_ml_outputs[] = {
    { .bus = &g_report_ml_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_report_out_safe },
};

BM_LET_DEFINE(task_report_isr, 2u, 0u, 100u, report_isr_step, NULL,
              k_report_isr_inputs, k_report_isr_outputs);
BM_LET_DEFINE(task_report_ml, 4u, 0u, 50u, report_ml_step, NULL,
              k_report_ml_inputs, k_report_ml_outputs);
BM_SCHEDULE_DEFINE(sched_report, 1000u, &task_report_isr, &task_report_ml);

#define REPORT_TEST_BUF_SIZE 4096u
static char g_report_buf[REPORT_TEST_BUF_SIZE];
static size_t g_report_off;

/** @brief 收集 bm_tt_schedule_report 逐行输出，拼接进静态缓冲供 strstr 断言 */
static void report_collect(const char *line, void *u) {
    size_t len = strlen(line);

    (void)u;
    if (g_report_off + len + 2u <= REPORT_TEST_BUF_SIZE) {
        (void)memcpy(g_report_buf + g_report_off, line, len);
        g_report_off += len;
        g_report_buf[g_report_off++] = '\n';
        g_report_buf[g_report_off] = '\0';
    }
}

/**
 * @brief rt_slot_count/at：数 ISR 域 activity 个数、按 idx 导出中立描述符、
 * 越界返回 BM_ERR_INVALID。复用场景 7 的 sched_freq（三个 ISR 域任务）。
 */
void test_rt_slot_count_and_at_isr_domain(void) {
    bm_tt_schedule_rt_slot_t slot;

    TEST_ASSERT_EQUAL_UINT32(3u, bm_tt_schedule_rt_slot_count(&sched_freq));

    /* idx=1 对应第二个 ISR 域 activity：task_estimator，every=5 */
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_rt_slot_at(&sched_freq, 1u, &slot));
    TEST_ASSERT_EQUAL_UINT32(1000u * 5u, slot.period_us);
    TEST_ASSERT_EQUAL_UINT32(slot.period_us, slot.deadline_us);
    TEST_ASSERT_EQUAL(BM_TT_DOMAIN_ISR, slot.domain);

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tt_schedule_rt_slot_at(&sched_freq, 3u, &slot));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_tt_schedule_rt_slot_at(&sched_freq, 100u, &slot));
}

/**
 * @brief report：表头含固定子串「[时间来源: 声明 wcet_us · 计划视图]」，
 * 峰值格一行含 ≤ 与 minor 标记，ISR/MAINLOOP 两块均有输出。
 */
void test_report_contains_header_and_peak_frame_markers(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_report_isr_in_bus, &report_isr_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_report_isr_out_bus, &report_isr_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_report_ml_in_bus, &report_ml_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_report_ml_out_bus, &report_ml_out_bus_storage, &cfg));

    task_report_ml.domain = BM_TT_DOMAIN_MAINLOOP;

    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_report));

    g_report_off = 0u;
    g_report_buf[0] = '\0';
    bm_tt_schedule_report(&sched_report, report_collect, NULL);

    TEST_ASSERT_NOT_NULL(strstr(g_report_buf, "[时间来源: 声明 wcet_us · 计划视图]"));
    TEST_ASSERT_NOT_NULL(strstr(g_report_buf, "\xe2\x89\xa4")); /* "≤" 的 UTF-8 编码 */
    TEST_ASSERT_NOT_NULL(strstr(g_report_buf, "minor"));
    TEST_ASSERT_NOT_NULL(strstr(g_report_buf, "task_report_isr"));
    TEST_ASSERT_NOT_NULL(strstr(g_report_buf, "task_report_ml"));

    bm_bus_close(&g_report_isr_in_bus);
    bm_bus_close(&g_report_isr_out_bus);
    bm_bus_close(&g_report_ml_in_bus);
    bm_bus_close(&g_report_ml_out_bus);
}

/* =========================================================================
 * 场景 13：ISR 低频任务（every=10）双缓冲边界发布（Task 8 · A1 telemetry）
 *   telemetry 一类低频任务 every=10 相比 every=2（场景3）更贴近真实错峰场景；
 *   复用 phase_step（通用透传：把输入原样写入输出），验证双缓冲翻转与命中
 *   频次无关，排除"只在 every=2 时凑巧对"的疑虑。
 * ========================================================================= */

BM_BUS_DEFINE(tele10_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(tele10_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_tele10_in_bus;
static bm_bus_t g_tele10_out_bus;

static const uint32_t k_tele10_in_safe = 0u;
static const uint32_t k_tele10_out_safe = 0u;

static const bm_let_input_t k_tele10_inputs[] = {
    { .bus = &g_tele10_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_tele10_in_safe },
};
static const bm_let_output_t k_tele10_outputs[] = {
    { .bus = &g_tele10_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_tele10_out_safe },
};

BM_LET_DEFINE(task_tele10, 10u, 0u, 100u, phase_step, NULL,
              k_tele10_inputs, k_tele10_outputs);
BM_SCHEDULE_DEFINE(sched_tele10, 1000u, &task_tele10);

/**
 * @brief every=10 低频任务连续 3 次命中：双缓冲同样正确翻转（+1 拍延迟），
 * 不因命中间隔变长而停在同一份缓冲（场景3 只验证了 every=2，本场景补低频档）。
 */
void test_isr_telemetry_double_buffer_flip_low_rate(void) {
    uint32_t out_val;
    uint32_t inputs[3] = { 111u, 222u, 333u };
    uint32_t hit = 0u;

    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_tele10_in_bus, &tele10_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_tele10_out_bus, &tele10_out_bus_storage, &cfg));

    sched_tele10.n_frames = 10u;
    sched_tele10.tick_idx = 0u;

    for (uint32_t tick_call = 0u; tick_call <= 20u; ++tick_call) {
        int is_hit = ((sched_tele10.tick_idx % task_tele10.every) == task_tele10.at);

        if (is_hit) {
            TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_tele10_in_bus, inputs[hit]));
        }
        bm_tt_schedule_tick(&sched_tele10);

        if (is_hit) {
            int rc = bm_bus_latest_read(&g_tele10_out_bus, &out_val);
            TEST_ASSERT_EQUAL(BM_OK, rc);
            if (hit == 0u) {
                TEST_ASSERT_EQUAL_UINT32(0u, out_val); /* outbuf 初值（未走 init 预填） */
            } else {
                TEST_ASSERT_EQUAL_UINT32(inputs[hit - 1u], out_val);
            }
            ++hit;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3u, hit);

    bm_bus_close(&g_tele10_in_bus);
    bm_bus_close(&g_tele10_out_bus);
}

/* =========================================================================
 * 场景 14：ISR + MAINLOOP 同表混跑——数据流正确 + 主循环饿死可见
 *   （Task 8 · A1-MAINLOOP）
 *   一张调度表同时装 ISR 域与 MAINLOOP 域任务：ISR 域每拍正常流动；
 *   MAINLOOP 域若干拍不调 run_pending 时，其输出 bus 的 seq 严格不变
 *   （"主循环饿死"外部可观测证据），直到 run_pending 消化后才在下一拍发布。
 * ========================================================================= */

BM_BUS_DEFINE(mix_isr_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(mix_isr_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(mix_ml_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(mix_ml_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_mix_isr_in_bus;
static bm_bus_t g_mix_isr_out_bus;
static bm_bus_t g_mix_ml_in_bus;
static bm_bus_t g_mix_ml_out_bus;

static const uint32_t k_mix_isr_in_safe = 0u;
static const uint32_t k_mix_isr_out_safe = 0u;
static const uint32_t k_mix_ml_in_safe = 0u;
static const uint32_t k_mix_ml_out_safe = 0u;

static const bm_let_input_t k_mix_isr_inputs[] = {
    { .bus = &g_mix_isr_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_mix_isr_in_safe },
};
static const bm_let_output_t k_mix_isr_outputs[] = {
    { .bus = &g_mix_isr_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_mix_isr_out_safe },
};
static const bm_let_input_t k_mix_ml_inputs[] = {
    { .bus = &g_mix_ml_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_mix_ml_in_safe },
};
static const bm_let_output_t k_mix_ml_outputs[] = {
    { .bus = &g_mix_ml_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_mix_ml_out_safe },
};

BM_LET_DEFINE(task_mix_isr, 1u, 0u, 50u, phase_step, NULL,
              k_mix_isr_inputs, k_mix_isr_outputs);
BM_LET_DEFINE(task_mix_ml, 1u, 0u, 50u, phase_step, NULL,
              k_mix_ml_inputs, k_mix_ml_outputs);
BM_SCHEDULE_DEFINE(sched_mix, 1000u, &task_mix_isr, &task_mix_ml);

/**
 * @brief ISR 域任务每拍正常流动；MAINLOOP 域任务在未调用 run_pending 期间
 * 输出 bus 的 seq 严格不变（连续两拍验证，非只看一拍），run_pending 消化
 * 后于下一拍恢复发布——同一张表两域混跑互不干扰。
 */
void test_mixed_isr_mainloop_same_table_dataflow_and_starvation(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t isr_val;
    uint32_t ml_val;
    uint32_t ml_seq0;
    uint32_t ml_seq1;
    uint32_t ml_seq2;
    uint32_t ran;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mix_isr_in_bus, &mix_isr_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mix_isr_out_bus, &mix_isr_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mix_ml_in_bus, &mix_ml_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_mix_ml_out_bus, &mix_ml_out_bus_storage, &cfg));

    task_mix_ml.domain = BM_TT_DOMAIN_MAINLOOP;
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_mix));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&g_mix_ml_out_bus, &ml_val, &ml_seq0));

    /* 第 1 拍：ISR 正常起步（发布 init 预填安全值）；MAINLOOP 只冻结、置 pending */
    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mix_isr_in_bus, 111u));
    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mix_ml_in_bus, 222u));
    bm_tt_schedule_tick(&sched_mix);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_mix_isr_out_bus, &isr_val));
    TEST_ASSERT_EQUAL_UINT32(k_mix_isr_out_safe, isr_val); /* ISR 首拍安全值 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&g_mix_ml_out_bus, &ml_val, &ml_seq1));
    TEST_ASSERT_EQUAL_UINT32(ml_seq0, ml_seq1); /* 未调 run_pending：MAINLOOP 输出 seq 不变 */

    /* 第 2 拍：仍不调 run_pending → MAINLOOP overrun+1，输出 seq 依旧冻结 */
    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mix_isr_in_bus, 112u));
    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mix_ml_in_bus, 223u));
    bm_tt_schedule_tick(&sched_mix);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_mix_isr_out_bus, &isr_val));
    TEST_ASSERT_EQUAL_UINT32(111u, isr_val); /* ISR 域数据流照常 +1 拍 */
    TEST_ASSERT_EQUAL_UINT32(1u, task_mix_ml.rt->overrun_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&g_mix_ml_out_bus, &ml_val, &ml_seq2));
    TEST_ASSERT_EQUAL_UINT32(ml_seq0, ml_seq2); /* 连续两拍饿死：seq 仍未变 */

    /* 主循环终于跑一次：消化 pending（用第 1 拍冻结的 222） */
    ran = bm_tt_schedule_run_pending(&sched_mix, 8u);
    TEST_ASSERT_EQUAL_UINT32(1u, ran);
    TEST_ASSERT_EQUAL(0, task_mix_ml.rt->pending);
    TEST_ASSERT_EQUAL(1, task_mix_ml.rt->fresh);

    /* 第 3 拍：MAINLOOP 发布 run_pending 算出的结果，seq 终于前进 */
    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_mix_isr_in_bus, 113u));
    bm_tt_schedule_tick(&sched_mix);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_mix_isr_out_bus, &isr_val));
    TEST_ASSERT_EQUAL_UINT32(112u, isr_val); /* ISR 域继续照常流动，未受 MAINLOOP 影响 */
    {
        uint32_t ml_seq3;

        TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read_seq(&g_mix_ml_out_bus, &ml_val, &ml_seq3));
        TEST_ASSERT_EQUAL_UINT32(222u, ml_val);        /* 第1拍冻结的输入算出的结果 */
        TEST_ASSERT_TRUE(ml_seq0 != ml_seq3);           /* seq 确已前进 */
    }

    bm_bus_close(&g_mix_isr_in_bus);
    bm_bus_close(&g_mix_isr_out_bus);
    bm_bus_close(&g_mix_ml_in_bus);
    bm_bus_close(&g_mix_ml_out_bus);
}

/* =========================================================================
 * 场景 15：真 init 后首拍安全值坐实（Task 8 · A4①）
 *   区别于场景10（只验证 init 后、任何 tick 之前可读到 safe_default）：
 *   本场景额外验证"第一次真实 tick 之后"下游读到的仍是 safe_default，
 *   而不是第一次 step 算出的结果——+1 拍延迟对首拍同样成立，不因为
 *   init 预填就被打破。
 * ========================================================================= */

BM_BUS_DEFINE(a4first_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(a4first_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_a4first_in_bus;
static bm_bus_t g_a4first_out_bus;

static const uint32_t k_a4first_in_safe = 0x11u;
static const uint32_t k_a4first_out_safe = 0x99u; /* 与真实业务输入区间(500+)不重叠 */

static const bm_let_input_t k_a4first_inputs[] = {
    { .bus = &g_a4first_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_a4first_in_safe },
};
static const bm_let_output_t k_a4first_outputs[] = {
    { .bus = &g_a4first_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_a4first_out_safe },
};

BM_LET_DEFINE(task_a4_first, 1u, 0u, 50u, phase_step, NULL,
              k_a4first_inputs, k_a4first_outputs);
BM_SCHEDULE_DEFINE(sched_a4_first, 1000u, &task_a4_first);

/**
 * @brief init 后立即可读 safe_default（同场景10）；紧接着第一次真实 tick
 * 之后仍读到 safe_default（不是刚算出的 500）；第二次 tick 才发布第一次
 * step 的结果——坐实首拍安全值"值正确"且"发布仍安全"。
 */
void test_init_first_real_tick_still_publishes_safe_default(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t out_val;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a4first_in_bus, &a4first_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a4first_out_bus, &a4first_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_a4_first));

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_a4first_out_bus, &out_val));
    TEST_ASSERT_EQUAL_UINT32(k_a4first_out_safe, out_val); /* init 后、tick 前 */

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_a4first_in_bus, 500u));
    bm_tt_schedule_tick(&sched_a4_first);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_a4first_out_bus, &out_val));
    TEST_ASSERT_EQUAL_UINT32(k_a4first_out_safe, out_val); /* 首拍仍安全，非刚算出的 500 */

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_a4first_in_bus, 501u));
    bm_tt_schedule_tick(&sched_a4_first);
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_a4first_out_bus, &out_val));
    TEST_ASSERT_EQUAL_UINT32(500u, out_val); /* 第2拍发布第1拍算出的结果 */

    bm_bus_close(&g_a4first_in_bus);
    bm_bus_close(&g_a4first_out_bus);
}

/* =========================================================================
 * 场景 16：显式 max_age_us 超龄 stale——区别于"从未发布"（Task 8 · A4②）
 *   场景1（never published）走的是冻结失败（rc!=BM_OK）分支，snapshot 强制
 *   safe_default；本场景走"曾发布过、后续不再更新"的 miss 累计分支——数据
 *   仍在（snapshot 保留最后发布值），只是 age 超过显式 max_age_us 才置 stale，
 *   两条路径语义不同，需分别坐实。
 * ========================================================================= */

BM_BUS_DEFINE(a4stale_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(a4stale_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_a4stale_in_bus;
static bm_bus_t g_a4stale_out_bus;

static const uint32_t k_a4stale_in_safe = 0u;
static const uint32_t k_a4stale_out_safe = 0u;

static int      g_a4stale_stale;
static uint32_t g_a4stale_age;
static uint32_t g_a4stale_snapshot_val;

/** @brief 只读输入、记录 stale/age/快照值，供区分"过期"与"从未发布"两路径 */
static void a4_stale_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const void *p;

    (void)state;
    p = bm_let_in(ctx, 0u, &stale, &age);
    g_a4stale_stale = stale;
    g_a4stale_age = age;
    g_a4stale_snapshot_val = *(const uint32_t *)p;
}

static const bm_let_input_t k_a4stale_inputs[] = {
    { .bus = &g_a4stale_in_bus, .max_age_us = 500u, /* 显式：半个任务周期 */
      .elem_size = sizeof(uint32_t), .safe_default = &k_a4stale_in_safe },
};
static const bm_let_output_t k_a4stale_outputs[] = {
    { .bus = &g_a4stale_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_a4stale_out_safe },
};

BM_LET_DEFINE(task_a4_stale, 1u, 0u, 50u, a4_stale_step, NULL,
              k_a4stale_inputs, k_a4stale_outputs);
BM_SCHEDULE_DEFINE(sched_a4_stale, 1000u, &task_a4_stale);

/**
 * @brief 发布一次后停发：miss=1(age=1000>500) 即置 stale=1，但 snapshot 仍是
 * 最后发布的真实值（非 safe_default）——与"从未发布"（场景1，snapshot 强制
 * safe_default）区分开的关键断言。
 */
void test_freeze_stale_via_explicit_max_age_after_prior_publish(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a4stale_in_bus, &a4stale_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a4stale_out_bus, &a4stale_out_bus_storage, &cfg));

    sched_a4_stale.n_frames = 1u;
    sched_a4_stale.tick_idx = 0u;

    TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_a4stale_in_bus, 777u));
    bm_tt_schedule_tick(&sched_a4_stale);
    TEST_ASSERT_EQUAL(0, g_a4stale_stale);
    TEST_ASSERT_EQUAL_UINT32(0u, g_a4stale_age);
    TEST_ASSERT_EQUAL_UINT32(777u, g_a4stale_snapshot_val);

    /* 停发第 1 拍：miss=1(age=1000>500) 即置 stale，snapshot 仍是 777（曾发布过） */
    bm_tt_schedule_tick(&sched_a4_stale);
    TEST_ASSERT_EQUAL(1, g_a4stale_stale);
    TEST_ASSERT_EQUAL_UINT32(1000u, g_a4stale_age);
    TEST_ASSERT_EQUAL_UINT32(777u, g_a4stale_snapshot_val); /* 非 safe_default，区别于"从未发布" */

    /* 停发第 2 拍：持续 stale */
    bm_tt_schedule_tick(&sched_a4_stale);
    TEST_ASSERT_EQUAL(1, g_a4stale_stale);
    TEST_ASSERT_EQUAL_UINT32(2000u, g_a4stale_age);
    TEST_ASSERT_EQUAL_UINT32(777u, g_a4stale_snapshot_val);

    bm_bus_close(&g_a4stale_in_bus);
    bm_bus_close(&g_a4stale_out_bus);
}

/* =========================================================================
 * 场景 17：seq-delta 判龄——快生产者方向（Task 8 · A4④）
 *   场景2 测的是慢生产者（每3拍发一次）+ 每拍消费；本场景反过来：生产者
 *   每次 tick 调用都发新值（比消费者快），消费者 every=3（慢）。因为只要
 *   两次冻结之间 seq 变过就清零 miss，快生产者方向下 miss/age 恒为 0——
 *   与慢生产者方向（miss 在 0..(every-1) 间摆动）形成对照。
 * ========================================================================= */

BM_BUS_DEFINE(a4fast_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(a4fast_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_a4fast_in_bus;
static bm_bus_t g_a4fast_out_bus;

static const uint32_t k_a4fast_in_safe = 0u;
static const uint32_t k_a4fast_out_safe = 0u;

static int      g_a4fast_stale;
static uint32_t g_a4fast_age;

static void a4_fastprod_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;

    (void)state;
    (void)bm_let_in(ctx, 0u, &stale, &age);
    g_a4fast_stale = stale;
    g_a4fast_age = age;
}

static const bm_let_input_t k_a4fast_inputs[] = {
    { .bus = &g_a4fast_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_a4fast_in_safe },
};
static const bm_let_output_t k_a4fast_outputs[] = {
    { .bus = &g_a4fast_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_a4fast_out_safe },
};

BM_LET_DEFINE(task_a4_fastprod, 3u, 0u, 50u, a4_fastprod_step, NULL,
              k_a4fast_inputs, k_a4fast_outputs);
BM_SCHEDULE_DEFINE(sched_a4_fastprod, 1000u, &task_a4_fastprod);

/**
 * @brief 生产者每次 tick 调用都发新值（every=3 消费者慢于生产者）：
 * 每次命中冻结时 seq 必已跳变，miss 恒为 0、age 恒为 0——与场景2
 * （慢生产者，miss 在 0..2 摆动）方向相反，坐实 A4④ 两个方向都对。
 */
void test_freeze_seq_delta_fast_producer_slow_consumer(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t v = 1u;
    uint32_t hits = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a4fast_in_bus, &a4fast_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a4fast_out_bus, &a4fast_out_bus_storage, &cfg));

    sched_a4_fastprod.n_frames = 3u;
    sched_a4_fastprod.tick_idx = 0u;

    for (uint32_t i = 0u; i < 9u; ++i) {
        int is_hit = ((sched_a4_fastprod.tick_idx % task_a4_fastprod.every) ==
                      task_a4_fastprod.at);

        TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_a4fast_in_bus, v));
        ++v;
        bm_tt_schedule_tick(&sched_a4_fastprod);

        if (is_hit) {
            TEST_ASSERT_EQUAL(0, g_a4fast_stale);
            TEST_ASSERT_EQUAL_UINT32(0u, g_a4fast_age);
            ++hits;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3u, hits);

    bm_bus_close(&g_a4fast_in_bus);
    bm_bus_close(&g_a4fast_out_bus);
}

/* =========================================================================
 * 场景 18：A3-lite 端到端数据流——input→compute(LET)→output 完整链（Task 8）
 *   真 init → 连续多拍 tick：断言输出随输入按 +1 拍延迟正确流动、含首拍
 *   安全值、以及一次真实 STALE 降级（暂停发布触发 stale 后 step 输出预设
 *   的 fail-safe 值，而非任意脏值；恢复发布后数据流照常接续）。真平衡车
 *   A3（真实多任务/多总线业务链）留给后续批次，本场景只需在 native 上
 *   证明"数据确实端到端流动"这条链本身是通的。
 * ========================================================================= */

BM_BUS_DEFINE(e2e_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(e2e_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_e2e_in_bus;
static bm_bus_t g_e2e_out_bus;

static const uint32_t k_e2e_in_safe = 0xAAu;
static const uint32_t k_e2e_out_safe = 0xFEEDu; /* fail-safe 降级值，兼作 init 预发布安全值 */

/** @brief 端到端 compute：stale 则输出降级到 safe_default，否则透传输入 */
static void e2e_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;
    const uint32_t *in;
    uint32_t *out;

    (void)state;
    in = (const uint32_t *)bm_let_in(ctx, 0u, &stale, &age);
    out = (uint32_t *)bm_let_out(ctx, 0u);
    *out = stale ? k_e2e_out_safe : *in;
}

static const bm_let_input_t k_e2e_inputs[] = {
    { .bus = &g_e2e_in_bus, .max_age_us = 1000u, /* 显式：等于 1 个任务周期 */
      .elem_size = sizeof(uint32_t), .safe_default = &k_e2e_in_safe },
};
static const bm_let_output_t k_e2e_outputs[] = {
    { .bus = &g_e2e_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_e2e_out_safe },
};

BM_LET_DEFINE(task_e2e, 1u, 0u, 50u, e2e_step, NULL, k_e2e_inputs, k_e2e_outputs);
BM_SCHEDULE_DEFINE(sched_e2e, 1000u, &task_e2e);

/**
 * @brief 10 拍端到端数据流：i=5..7 暂停发布触发 stale（miss=2 时
 * age=2000>max_age=1000），i=6/7 两拍 compute 降级输出 k_e2e_out_safe；
 * 恢复发布后 i=9 起数据流复原。断言逐拍发布值验证 +1 拍延迟贯穿全程
 * （含首拍安全值与 STALE 降级两个关键点），非任意脏值。
 */
void test_e2e_datapath_first_tick_safe_then_flow_then_stale_then_recover(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    /* i=5..7 暂停发布（值不使用，仅占位标注不发布） */
    uint32_t vals[10] = { 1000u, 1001u, 1002u, 1003u, 1004u, 0u, 0u, 0u, 1008u, 1009u };
    uint32_t expect[10] = {
        k_e2e_out_safe, /* 0：首拍安全值 */
        1000u, 1001u, 1002u, 1003u, 1004u, /* 1..5：稳态 +1 拍 */
        1004u,          /* 6：i=5 未发布，仍在 max_age 内(miss=1,age=1000)未 stale，
                            透传上次输入 → 发布的是 i=5 算出的 1004 */
        k_e2e_out_safe, /* 7：i=6 已 stale(miss=2,age=2000>1000)，降级 */
        k_e2e_out_safe, /* 8：i=7 仍 stale，继续降级 */
        1008u           /* 9：i=8 恢复发布，数据流复原 */
    };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_e2e_in_bus, &e2e_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_e2e_out_bus, &e2e_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_e2e));

    for (uint32_t i = 0u; i < 10u; ++i) {
        uint32_t out_val;

        if (i < 5u || i >= 8u) {
            TEST_ASSERT_EQUAL(BM_OK, publish_u32(&g_e2e_in_bus, vals[i]));
        } /* i=5,6,7：暂停发布，触发 stale */

        bm_tt_schedule_tick(&sched_e2e);

        TEST_ASSERT_EQUAL(BM_OK, bm_bus_latest_read(&g_e2e_out_bus, &out_val));
        TEST_ASSERT_EQUAL_UINT32(expect[i], out_val);
    }

    bm_bus_close(&g_e2e_in_bus);
    bm_bus_close(&g_e2e_out_bus);
}

/* =========================================================================
 * 场景 19：确定性不变量回归锁（Task 8 · A5）
 *
 *   本门面的确定性证据链：
 *   ① 零动态分配——`BM_LET_DEFINE`/`BM_SCHEDULE_DEFINE` 分配的 snapshot/
 *      双缓冲/rt bookkeeping/entries 指针表全部是编译期静态数组；已人工
 *      核对 `Source/hybrid/bm_tt_schedule.c` 全文件不含 malloc/calloc/
 *      realloc/free 符号（grep 确认为空）。因此本门面在设计上就不可能
 *      触发动态分配，不需要像 `test_det_zeroalloc.c` 那样接入陷阱分配器
 *      （该机制面向"可能调用 malloc"的模块；本门面从源头排除了这个可能）。
 *      同时复用仓内既有的 `determinism` ctest 标签惯例（见本文件对应
 *      CMakeLists.txt 里 `set_tests_properties(test_tt_schedule PROPERTIES
 *      LABELS "determinism")`），纳入 `ctest -L determinism` 回归面。
 *   ② 无新增无界循环——`bm_tt_schedule.c` 内所有运行期循环的上界均是
 *      init 期已校验过的静态量：`entry_count ≤ BM_CONFIG_TT_SCHED_
 *      MAX_ENTRIES`、`input_count`（显式 ≤ `BM_CONFIG_TT_SCHED_MAX_INPUTS`，
 *      `output_count` 为 uint8_t 自身天然有界）、`n_frames ≤
 *      BM_CONFIG_TT_SCHED_MAX_FRAMES`（`tt_frame_check` 里的 `w[]` 静态
 *      数组同一上界）。本测试用一张真实 init 过的调度表把这些边界关系
 *      断言下来，作为该不变量的回归锁——任何未来改动打破边界校验都会在
 *      此报红，而非自造一套重的确定性验证框架。
 * ========================================================================= */

BM_BUS_DEFINE(a5_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(a5_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_a5_in_bus;
static bm_bus_t g_a5_out_bus;

static const uint32_t k_a5_in_safe = 0u;
static const uint32_t k_a5_out_safe = 0u;

static const bm_let_input_t k_a5_inputs[] = {
    { .bus = &g_a5_in_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_a5_in_safe },
};
static const bm_let_output_t k_a5_outputs[] = {
    { .bus = &g_a5_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_a5_out_safe },
};

BM_LET_DEFINE(task_a5, 4u, 0u, 10u, overload_noop_step, NULL,
              k_a5_inputs, k_a5_outputs);
BM_SCHEDULE_DEFINE(sched_a5, 1000u, &task_a5);

/**
 * @brief 真实 init 之后，entry_count/input_count/output_count/n_frames/
 * elem_size 均落在编译期配置上界内——确定性不变量②的回归锁（见本场景
 * 头部注释①②）。
 */
void test_a5_determinism_invariants_bounded_after_init(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a5_in_bus, &a5_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_a5_out_bus, &a5_out_bus_storage, &cfg));

    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_a5));

    TEST_ASSERT_TRUE(sched_a5.entry_count <= BM_CONFIG_TT_SCHED_MAX_ENTRIES);
    TEST_ASSERT_TRUE(sched_a5.n_frames <= BM_CONFIG_TT_SCHED_MAX_FRAMES);
    TEST_ASSERT_TRUE(task_a5.input_count <= BM_CONFIG_TT_SCHED_MAX_INPUTS);
    TEST_ASSERT_TRUE(task_a5.inputs[0].elem_size <= BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE);
    TEST_ASSERT_TRUE(task_a5.outputs[0].elem_size <= BM_CONFIG_TT_SCHED_MAX_ELEM_SIZE);

    bm_bus_close(&g_a5_in_bus);
    bm_bus_close(&g_a5_out_bus);
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
    RUN_TEST(test_init_rejects_frame_overload);
    RUN_TEST(test_init_rejects_lcm_explosion);
    RUN_TEST(test_init_pre_publishes_safe_default_before_any_tick);
    RUN_TEST(test_init_harmonic_periods_ok_and_runs_data_flow);
    RUN_TEST(test_rt_slot_count_and_at_isr_domain);
    RUN_TEST(test_report_contains_header_and_peak_frame_markers);
    RUN_TEST(test_isr_telemetry_double_buffer_flip_low_rate);
    RUN_TEST(test_mixed_isr_mainloop_same_table_dataflow_and_starvation);
    RUN_TEST(test_init_first_real_tick_still_publishes_safe_default);
    RUN_TEST(test_freeze_stale_via_explicit_max_age_after_prior_publish);
    RUN_TEST(test_freeze_seq_delta_fast_producer_slow_consumer);
    RUN_TEST(test_e2e_datapath_first_tick_safe_then_flow_then_stale_then_recover);
    RUN_TEST(test_a5_determinism_invariants_bounded_after_init);
    return UNITY_END();
}

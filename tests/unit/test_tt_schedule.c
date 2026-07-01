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

/** @brief 只读输入、记录 stale/age，不写输出（bm_let_out 是 Task4 桩，本任务不调） */
static void never_step(bm_let_ctx_t *ctx, void *state) {
    int stale;
    uint32_t age;

    (void)state;
    (void)bm_let_in(ctx, 0u, &stale, &age);
    g_never_stale = stale;
    g_never_age = age;
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

    g_never_stale = 0; g_never_age = 0u;
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_freeze_stale_when_never_published);
    RUN_TEST(test_freeze_seq_delta_slow_producer);
    return UNITY_END();
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_tt_wcet.c
 * @brief bm_tt_schedule × bm_wcet_mon 门控接入测试（spec §4 / 验收 A1-A2）
 */
#include "unity.h"
#include "bm_tt_schedule.h"
#include "bm_bus.h"
#include "bm/hybrid/bm_wcet_mon.h"
#include "bm_hal_uptime_native.h"

#include <string.h>

/* ---- 装配：ISR 域慢任务（step 内注入时间超预算）+ MAINLOOP 域任务 ---- */

BM_BUS_DEFINE(w_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(w_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(w_ml_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_w_in_bus;
static bm_bus_t g_w_out_bus;
static bm_bus_t g_w_ml_out_bus;

static const uint32_t k_w_safe = 0u;

static uint32_t g_isr_step_cost_us; /**< ISR step 模拟耗时（advance 注入） */

/** @brief ISR 域 step：注入 g_isr_step_cost_us 的模拟耗时 */
static void isr_step(bm_let_ctx_t *ctx, void *state) {
    int stale; uint32_t age;
    (void)state;
    (void)bm_let_in(ctx, 0u, &stale, &age);
    bm_hal_uptime_native_advance_us(g_isr_step_cost_us);
    *(uint32_t *)bm_let_out(ctx, 0u) = 1u;
}

/** @brief MAINLOOP 域 step：固定注入 30µs */
static void ml_step(bm_let_ctx_t *ctx, void *state) {
    int stale; uint32_t age;
    (void)state;
    (void)bm_let_in(ctx, 0u, &stale, &age);
    bm_hal_uptime_native_advance_us(30u);
    *(uint32_t *)bm_let_out(ctx, 0u) = 2u;
}

static const bm_let_input_t k_w_inputs[] = {
    { .bus = &g_w_in_bus, .max_age_us = 0u,
      .elem_size = sizeof(uint32_t), .safe_default = &k_w_safe },
};
static const bm_let_output_t k_w_outputs[] = {
    { .bus = &g_w_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_w_safe },
};
static const bm_let_output_t k_w_ml_outputs[] = {
    { .bus = &g_w_ml_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_w_safe },
};

BM_LET_DEFINE_ISR(task_w_isr, 1u, 0u, 100u, isr_step, NULL, k_w_inputs, k_w_outputs);
BM_LET_DEFINE_MAINLOOP(task_w_ml, 2u, 0u, 50u, ml_step, NULL, k_w_inputs, k_w_ml_outputs);
BM_SCHEDULE_DEFINE(sched_w, 1000u, &task_w_isr, &task_w_ml);

/* ---- sink 捕获 ---- */
static uint32_t g_evt_overrun_n;
static uint32_t g_evt_miss_n;
static const bm_wcet_span_t *g_last_span;

/** @brief 测试 sink：分类计数 */
static void tt_sink(const bm_wcet_span_t *span, bm_wcet_evt_t evt,
                    uint32_t measured_us, void *user) {
    (void)measured_us; (void)user;
    g_last_span = span;
    if (evt == BM_WCET_EVT_BUDGET_OVERRUN) { g_evt_overrun_n++; } else { g_evt_miss_n++; }
}

/** @brief 按名字找已注册 span（TT 池藏 .c 内部，测试经迭代面取） */
static const bm_wcet_span_t *span_by_name(const char *name) {
    for (uint32_t i = 0u; i < bm_wcet_mon_span_count(); ++i) {
        const bm_wcet_span_t *sp = bm_wcet_mon_span_at(i);
        if (strcmp(sp->name, name) == 0) { return sp; }
    }
    return NULL;
}

void setUp(void) {
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_hal_uptime_native_reset();
    bm_wcet_mon_init();
    g_evt_overrun_n = 0u; g_evt_miss_n = 0u; g_last_span = NULL;
    g_isr_step_cost_us = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_w_in_bus, &w_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_w_out_bus, &w_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_w_ml_out_bus, &w_ml_out_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_w));
    bm_wcet_mon_set_sink(tt_sink, NULL);
}

void tearDown(void) {}

/** init 自动注册：每任务一 span，name/budget 对上 */
void test_init_registers_span_per_task(void) {
    const bm_wcet_span_t *sp_isr = span_by_name("task_w_isr");
    const bm_wcet_span_t *sp_ml = span_by_name("task_w_ml");
    TEST_ASSERT_NOT_NULL(sp_isr);
    TEST_ASSERT_NOT_NULL(sp_ml);
    TEST_ASSERT_EQUAL_UINT32(100u, sp_isr->budget_us);
    TEST_ASSERT_EQUAL_UINT32(50u, sp_ml->budget_us);
}

/** 重复 init 复用映射：span 数不翻倍、观测面复位 */
void test_reinit_reuses_span(void) {
    uint32_t n = bm_wcet_mon_span_count();
    TEST_ASSERT_EQUAL(BM_OK, bm_tt_schedule_init(&sched_w));
    TEST_ASSERT_EQUAL_UINT32(n, bm_wcet_mon_span_count());
    TEST_ASSERT_EQUAL_UINT32(0u, span_by_name("task_w_isr")->run_count);
}

/** A1：ISR 域 step 实测被记录；超预算触发 BUDGET_OVERRUN */
void test_isr_step_measured_and_overrun_detected(void) {
    const bm_wcet_span_t *sp = span_by_name("task_w_isr");

    g_isr_step_cost_us = 60u; /* 预算 100 内 */
    bm_tt_schedule_tick(&sched_w);
    TEST_ASSERT_EQUAL_UINT32(60u, sp->last_us);
    TEST_ASSERT_EQUAL_UINT32(0u, g_evt_overrun_n);

    g_isr_step_cost_us = 150u; /* 超预算 100 */
    bm_tt_schedule_tick(&sched_w);
    TEST_ASSERT_EQUAL_UINT32(150u, sp->last_us);
    TEST_ASSERT_EQUAL_UINT32(150u, sp->max_us);
    TEST_ASSERT_EQUAL_UINT32(1u, sp->overrun_count);
    TEST_ASSERT_EQUAL_UINT32(1u, g_evt_overrun_n);
    TEST_ASSERT_EQUAL_PTR(sp, g_last_span);
}

/** A1-MAINLOOP：run_pending 里 step 实测被记录 */
void test_mainloop_step_measured(void) {
    const bm_wcet_span_t *sp = span_by_name("task_w_ml");

    bm_tt_schedule_tick(&sched_w); /* every=2 at=0 命中：冻结 + pending */
    TEST_ASSERT_EQUAL_UINT32(1u, bm_tt_schedule_run_pending(&sched_w, 4u));
    TEST_ASSERT_EQUAL_UINT32(30u, sp->last_us);
    TEST_ASSERT_EQUAL_UINT32(1u, sp->run_count);
}

/** A2①：ISR reentry skip → DEADLINE_MISS 上报 + 门面/中央双账同步 */
void test_isr_reentry_skip_reports_miss(void) {
    const bm_wcet_span_t *sp = span_by_name("task_w_isr");

    task_w_isr.rt->running = 1u; /* 人工制造"上拍未完" */
    bm_tt_schedule_tick(&sched_w);
    task_w_isr.rt->running = 0u;
    TEST_ASSERT_EQUAL_UINT32(1u, task_w_isr.rt->overrun_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sp->miss_count);
    TEST_ASSERT_EQUAL_UINT32(1u, g_evt_miss_n);
}

/* ---- 池耗尽：第二张表 init 硬失败（spec §4.2）---- */

BM_BUS_DEFINE(w2_in_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(w2_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
static bm_bus_t g_w2_in_bus;
static bm_bus_t g_w2_out_bus;
static const bm_let_input_t k_w2_inputs[] = {
    { .bus = &g_w2_in_bus, .max_age_us = 0u,
      .elem_size = sizeof(uint32_t), .safe_default = &k_w_safe },
};
static const bm_let_output_t k_w2_outputs[] = {
    { .bus = &g_w2_out_bus, .elem_size = sizeof(uint32_t), .safe_default = &k_w_safe },
};
BM_LET_DEFINE_ISR(task_w2_isr, 1u, 0u, 10u, isr_step, NULL, k_w2_inputs, k_w2_outputs);
BM_SCHEDULE_DEFINE(sched_w2, 1000u, &task_w2_isr);

/** 注册表被填满后，新表 init 必须硬失败（宁可失败不悄悄不监控） */
void test_init_fails_when_registry_full(void) {
    static bm_wcet_span_t fillers[BM_CONFIG_WCET_MON_MAX_SPANS];
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    uint32_t i = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_w2_in_bus, &w2_in_bus_storage, &cfg));
    TEST_ASSERT_EQUAL(BM_OK, bm_bus_open(&g_w2_out_bus, &w2_out_bus_storage, &cfg));
    /* 用 filler 把注册表填满（setUp 后已有 sched_w 的 2 条） */
    while (bm_wcet_mon_span_count() < BM_CONFIG_WCET_MON_MAX_SPANS) {
        fillers[i].name = "filler"; fillers[i].budget_us = 0u;
        TEST_ASSERT_EQUAL(BM_OK, bm_wcet_mon_register(&fillers[i]));
        i++;
    }
    TEST_ASSERT_EQUAL(BM_ERR_NO_MEM, bm_tt_schedule_init(&sched_w2));
}

/** A2②：MAINLOOP pending 未消化 skip → DEADLINE_MISS 上报 */
void test_mainloop_pending_skip_reports_miss(void) {
    const bm_wcet_span_t *sp = span_by_name("task_w_ml");

    bm_tt_schedule_tick(&sched_w); /* tick0：冻结 + pending=1 */
    bm_tt_schedule_tick(&sched_w); /* tick1：every=2 未命中 */
    bm_tt_schedule_tick(&sched_w); /* tick2：命中，pending 仍 1 → skip */
    TEST_ASSERT_EQUAL_UINT32(1u, task_w_ml.rt->overrun_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sp->miss_count);
    TEST_ASSERT_EQUAL_UINT32(1u, g_evt_miss_n);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_registers_span_per_task);
    RUN_TEST(test_reinit_reuses_span);
    RUN_TEST(test_isr_step_measured_and_overrun_detected);
    RUN_TEST(test_mainloop_step_measured);
    RUN_TEST(test_isr_reentry_skip_reports_miss);
    RUN_TEST(test_mainloop_pending_skip_reports_miss);
    RUN_TEST(test_init_fails_when_registry_full);
    return UNITY_END();
}

/**
 * @file test_fault_derating.c
 * @brief fault_derating 组件单元测试
 *
 * 覆盖 ramp-down 降额、恢复计时、故障锁存与 NULL 边界。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-23       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/fault_derating.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 遥测回调统计 ---------- */
static uint32_t g_tel_count;
static float    g_last_derate_factor;
static uint32_t g_last_status;

static void tel_cb(void *user,
                   const bm_fault_derating_telemetry_t *telemetry) {
    (void)user;
    g_tel_count++;
    g_last_derate_factor = telemetry->derate_factor;
    g_last_status        = telemetry->status;
}

/* ---------- 标准 axis 构造辅助 ---------- */
static void make_axis(bm_fault_derating_axis_t *axis,
                      float rate_per_s,
                      float recovery_time_s,
                      float derate_target,
                      float dt_s) {
    memset(axis, 0, sizeof(*axis));
    axis->config.derate_ramp.rate_per_s = rate_per_s;
    axis->config.recovery_time_s        = recovery_time_s;
    axis->config.derate_target          = derate_target;
    axis->config.dt_s                   = dt_s;
    axis->resources.publish_telemetry      = tel_cb;
    axis->resources.publish_telemetry_user = NULL;
}

void setUp(void) {
    g_tel_count          = 0u;
    g_last_derate_factor = 1.0f;
    g_last_status        = 0u;
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

/**
 * @brief 正常 init，状态初始化为全额（derate_factor == 1.0）
 */
void test_fault_derating_init_ok(void) {
    bm_fault_derating_axis_t axis;
    make_axis(&axis, 10.0f, 0.1f, 0.5f, 0.01f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, axis.state.derate_factor);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
}

/**
 * @brief latch 后多步 step，derate_factor 向 derate_target ramp-down
 */
void test_fault_derating_ramp_down_after_latch(void) {
    bm_fault_derating_axis_t axis;
    float f_before;
    uint32_t i;

    /* rate=2.0/s, dt=0.1s → 每步最多降 0.2；5步内必然从 1.0 下降 */
    make_axis(&axis, 2.0f, 1.0f, 0.0f, 0.1f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));

    bm_fault_derating_latch(&axis);
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);

    f_before = axis.state.derate_factor;
    for (i = 0u; i < 5u; i++) {
        bm_fault_derating_step(&axis);
    }

    TEST_ASSERT_TRUE(axis.state.derate_factor < f_before);
}

/**
 * @brief 充分步进后 derate_factor 最终到达 derate_target
 */
void test_fault_derating_ramp_reaches_target(void) {
    bm_fault_derating_axis_t axis;
    uint32_t i;

    /* rate=10/s, dt=0.01s → 每步 0.1；10步可从1降到0 */
    make_axis(&axis, 10.0f, 1.0f, 0.0f, 0.01f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));
    bm_fault_derating_latch(&axis);

    for (i = 0u; i < 20u; i++) {
        bm_fault_derating_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, axis.state.derate_factor);
}

/**
 * @brief clear_request 后必须等待 recovery_time_s 才开始恢复
 */
void test_fault_derating_recovery_timer_guards_restore(void) {
    bm_fault_derating_axis_t axis;
    uint32_t i;
    float factor_before_recovery;

    /* recovery_time_s = 0.5s, dt = 0.01s → 需要 50 步才能开始恢复 */
    make_axis(&axis, 20.0f, 0.5f, 0.0f, 0.01f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));

    bm_fault_derating_latch(&axis);
    for (i = 0u; i < 30u; i++) { bm_fault_derating_step(&axis); }

    bm_fault_derating_clear_request(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);

    /* 前 49 步：recovery_elapsed < recovery_time_s，不应恢复 */
    factor_before_recovery = axis.state.derate_factor;
    for (i = 0u; i < 49u; i++) { bm_fault_derating_step(&axis); }
    /* derate_factor 不应增大 */
    TEST_ASSERT_TRUE(axis.state.derate_factor <= factor_before_recovery + 1e-4f);

    /* 第 50 步及之后才会开始 ramp-up */
    bm_fault_derating_step(&axis);
    TEST_ASSERT_TRUE(axis.state.derate_factor > factor_before_recovery + 1e-4f);
}

/**
 * @brief 恢复后充分步进，derate_factor 回到 1.0
 */
void test_fault_derating_recovery_reaches_full(void) {
    bm_fault_derating_axis_t axis;
    uint32_t i;

    /* rate=10/s, recovery=0.0s（立即恢复）, dt=0.01s */
    make_axis(&axis, 10.0f, 0.0f, 0.0f, 0.01f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));

    bm_fault_derating_latch(&axis);
    for (i = 0u; i < 20u; i++) { bm_fault_derating_step(&axis); }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, axis.state.derate_factor);

    bm_fault_derating_clear_request(&axis);
    for (i = 0u; i < 20u; i++) { bm_fault_derating_step(&axis); }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, axis.state.derate_factor);
}

/**
 * @brief 遥测回调：每次 step 均被调用，且 status 携带 LATCHED 位
 */
void test_fault_derating_telemetry_published_and_latched_bit(void) {
    bm_fault_derating_axis_t axis;
    uint32_t i;

    make_axis(&axis, 5.0f, 1.0f, 0.5f, 0.01f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));
    bm_fault_derating_latch(&axis);

    for (i = 0u; i < 3u; i++) { bm_fault_derating_step(&axis); }

    TEST_ASSERT_EQUAL(3u, g_tel_count);
    TEST_ASSERT_NOT_EQUAL(0u, g_last_status & BM_FAULT_DERATING_TEL_LATCHED);
    TEST_ASSERT_NOT_EQUAL(0u, g_last_status & BM_FAULT_DERATING_TEL_VALID);
    /* derate_factor 应在 (0, 1] 区间内 */
    TEST_ASSERT_TRUE(g_last_derate_factor <= 1.0f);
    TEST_ASSERT_TRUE(g_last_derate_factor >= 0.0f);
}

/**
 * @brief latch 期间遥测中 derate_factor 单调不增
 */
void test_fault_derating_factor_monotone_decreasing_while_latched(void) {
    bm_fault_derating_axis_t axis;
    float prev;
    uint32_t i;

    make_axis(&axis, 5.0f, 1.0f, 0.2f, 0.01f);
    TEST_ASSERT_EQUAL(BM_OK, bm_fault_derating_init(&axis));
    bm_fault_derating_latch(&axis);

    prev = axis.state.derate_factor;
    for (i = 0u; i < 20u; i++) {
        bm_fault_derating_step(&axis);
        TEST_ASSERT_TRUE(axis.state.derate_factor <= prev + 1e-5f);
        prev = axis.state.derate_factor;
    }
}

/* ---------- NULL 边界测试 ---------- */

/**
 * @brief init NULL 返回 BM_ERR_INVALID
 */
void test_fault_derating_init_null(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_fault_derating_init(NULL));
}

/**
 * @brief validate_config NULL 返回 BM_ERR_INVALID
 */
void test_fault_derating_validate_null(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_fault_derating_validate_config(NULL));
}

/**
 * @brief validate_config dt_s <= 0 返回 BM_ERR_INVALID
 */
void test_fault_derating_validate_bad_dt(void) {
    bm_fault_derating_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.derate_ramp.rate_per_s = 1.0f;
    cfg.recovery_time_s        = 0.1f;
    cfg.derate_target          = 0.5f;
    cfg.dt_s                   = 0.0f; /* 无效 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_fault_derating_validate_config(&cfg));
}

/**
 * @brief latch/clear_request/step/reset 传 NULL 不崩溃
 */
void test_fault_derating_null_safe_ops(void) {
    bm_fault_derating_latch(NULL);
    bm_fault_derating_clear_request(NULL);
    bm_fault_derating_step(NULL);
    bm_fault_derating_reset(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fault_derating_init_ok);
    RUN_TEST(test_fault_derating_ramp_down_after_latch);
    RUN_TEST(test_fault_derating_ramp_reaches_target);
    RUN_TEST(test_fault_derating_recovery_timer_guards_restore);
    RUN_TEST(test_fault_derating_recovery_reaches_full);
    RUN_TEST(test_fault_derating_telemetry_published_and_latched_bit);
    RUN_TEST(test_fault_derating_factor_monotone_decreasing_while_latched);
    RUN_TEST(test_fault_derating_init_null);
    RUN_TEST(test_fault_derating_validate_null);
    RUN_TEST(test_fault_derating_validate_bad_dt);
    RUN_TEST(test_fault_derating_null_safe_ops);
    return UNITY_END();
}

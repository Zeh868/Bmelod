/**
 * @file test_motion_coordination.c
 * @brief motion_coordination 组件单元测试
 *
 * 覆盖以下场景：
 *   1. 多轴 ramp 协调：两轴同步斜坡推进，位置逼近目标；
 *   2. set_targets：运行中动态更新目标，斜坡跟随；
 *   3. validate_config 边界：axis_count=0、dt_s<=0、rate_per_s<=0 被拒；
 *   4. exec_ops 生命周期：init → start → run → safe_stop；
 *   5. NULL 边界：接口传入 NULL 不崩溃。
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
#include "bm/component/motion_coordination.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/* ---------- 测试辅助 ---------- */

/** 最近一次遥测副本 */
static bm_motion_coordination_telemetry_t g_last_tel;
/** 遥测发布计数 */
static uint32_t g_tel_count;

/**
 * @brief 模拟 publish_telemetry 回调
 */
static void fake_publish(void *user,
                         const bm_motion_coordination_telemetry_t *tel) {
    (void)user;
    g_last_tel = *tel;
    g_tel_count++;
}

/**
 * @brief 构造两轴协调配置（rate=1.0/s，dt=0.01s）
 *
 * @param axis 待初始化的轴实例
 */
static void make_2axis(bm_motion_coordination_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.axis_count = 2u;
    axis->config.ramp[0].rate_per_s = 1.0f;
    axis->config.ramp[1].rate_per_s = 2.0f;
    axis->config.dt_s = 0.01f;
    axis->resources.publish_telemetry = fake_publish;
}

/* ---------- setUp / tearDown ---------- */

void setUp(void) {
    memset(&g_last_tel, 0, sizeof(g_last_tel));
    g_tel_count = 0u;
}

void tearDown(void) {}

/* ---------- 测试用例 ---------- */

/**
 * @brief 双轴斜坡经过足够步数后位置逼近各自目标
 *
 * 轴 0：rate=1.0/s，dt=0.01s，目标=0.5 → 需约 50 步
 * 轴 1：rate=2.0/s，dt=0.01s，目标=1.0 → 需约 50 步
 * 运行 100 步，两轴均应到达目标。
 */
void test_motion_coord_ramp_reaches_target(void) {
    bm_motion_coordination_axis_t axis;
    float targets[2] = {0.5f, 1.0f};
    uint32_t i;

    make_2axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_coordination_init(&axis));
    bm_motion_coordination_set_targets(&axis, targets);

    for (i = 0u; i < 100u; i++) {
        bm_motion_coordination_step(&axis);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, g_last_tel.position[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, g_last_tel.position[1]);
    TEST_ASSERT_EQUAL_UINT32(BM_MOTION_COORD_TEL_VALID, g_last_tel.status);
    TEST_ASSERT_EQUAL(2u, g_last_tel.axis_count);
    TEST_ASSERT_EQUAL(100u, g_tel_count);
}

/**
 * @brief 运行到目标 0.3 后动态改目标为 0.1，斜坡反向逼近
 */
void test_motion_coord_set_targets_dynamic(void) {
    bm_motion_coordination_axis_t axis;
    float targets[2];
    uint32_t i;

    make_2axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_coordination_init(&axis));

    targets[0] = 0.3f;
    targets[1] = 0.3f;
    bm_motion_coordination_set_targets(&axis, targets);

    for (i = 0u; i < 50u; i++) {
        bm_motion_coordination_step(&axis);
    }
    /* 轴 0 已接近 0.3 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, g_last_tel.position[0]);

    /* 改变目标到 0.1，继续运行 */
    targets[0] = 0.1f;
    targets[1] = 0.1f;
    bm_motion_coordination_set_targets(&axis, targets);

    for (i = 0u; i < 50u; i++) {
        bm_motion_coordination_step(&axis);
    }
    /* 应已回到 0.1 */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.1f, g_last_tel.position[0]);
}

/**
 * @brief validate_config：axis_count=0 被拒
 */
void test_motion_coord_validate_axis_count_zero(void) {
    bm_motion_coordination_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.axis_count = 0u;
    cfg.dt_s = 0.01f;
    cfg.ramp[0].rate_per_s = 1.0f;

    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_motion_coordination_validate_config(&cfg));
}

/**
 * @brief validate_config：dt_s <= 0 被拒
 */
void test_motion_coord_validate_dt_zero(void) {
    bm_motion_coordination_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.axis_count = 1u;
    cfg.dt_s = 0.0f; /* 非法 */
    cfg.ramp[0].rate_per_s = 1.0f;

    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_motion_coordination_validate_config(&cfg));
}

/**
 * @brief validate_config：rate_per_s <= 0 被拒
 */
void test_motion_coord_validate_rate_zero(void) {
    bm_motion_coordination_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.axis_count = 1u;
    cfg.dt_s = 0.01f;
    cfg.ramp[0].rate_per_s = 0.0f; /* 非法 */

    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_motion_coordination_validate_config(&cfg));
}

/**
 * @brief exec_ops 生命周期：init → start → run → safe_stop
 *
 * safe_stop 后各轴 target 应等于当前 ramp 输出（就地停止）。
 */
void test_motion_coord_exec_ops_lifecycle(void) {
    bm_motion_coordination_axis_t axis;
    bm_exec_t inst;
    float targets[2] = {1.0f, 1.0f};
    uint32_t i;
    float pos0_before_stop;

    make_2axis(&axis);
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_motion_coordination_exec_init(&inst));
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_coordination_exec_start(&inst));

    bm_motion_coordination_set_targets(&axis, targets);
    for (i = 0u; i < 30u; i++) {
        bm_motion_coordination_exec_run(&inst);
    }
    TEST_ASSERT_EQUAL(30u, g_tel_count);

    /* 记录 safe_stop 前的输出位置 */
    pos0_before_stop = axis.state.ramp[0].output;

    bm_motion_coordination_exec_safe_stop(&inst);

    /* target 应已锁定到当前输出 */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, pos0_before_stop,
                             axis.state.target[0]);
}

/**
 * @brief NULL 边界：各接口传入 NULL 不崩溃
 */
void test_motion_coord_null_safety(void) {
    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_motion_coordination_validate_config(NULL));
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_motion_coordination_init(NULL));
    bm_motion_coordination_reset(NULL, NULL);
    bm_motion_coordination_set_targets(NULL, NULL);
    bm_motion_coordination_step(NULL);

    /* exec_ops NULL 安全 */
    bm_motion_coordination_exec_run(NULL);
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_motion_coordination_exec_init(NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_coordination_exec_start(NULL));
    bm_motion_coordination_exec_safe_stop(NULL);
}

/* ---------- main ---------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motion_coord_ramp_reaches_target);
    RUN_TEST(test_motion_coord_set_targets_dynamic);
    RUN_TEST(test_motion_coord_validate_axis_count_zero);
    RUN_TEST(test_motion_coord_validate_dt_zero);
    RUN_TEST(test_motion_coord_validate_rate_zero);
    RUN_TEST(test_motion_coord_exec_ops_lifecycle);
    RUN_TEST(test_motion_coord_null_safety);
    return UNITY_END();
}

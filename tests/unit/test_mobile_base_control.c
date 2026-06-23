/**
 * @file test_mobile_base_control.c
 * @brief mobile_base_control 组件单元测试
 *
 * 覆盖：差速运动学 v,ω → 左右轮速换算、坡道前馈场景、
 * 差速运动学边界（最大轮速限幅）、exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补坡道前馈 / 边界 / exec_ops 测试用例
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/mobile_base_control.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static float g_left;
static float g_right;

static int write_wheels(void *user, float left_m_s, float right_m_s) {
    (void)user;
    g_left = left_m_s;
    g_right = right_m_s;
    return 0;
}

void setUp(void) {
    g_left = 0.0f;
    g_right = 0.0f;
}

void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* 测试用例 1：基础差速运动学 v=1 ω=0 和 v=0 ω=1                        */
/* ------------------------------------------------------------------ */
void test_mobile_base_differential_kinematics(void) {
    bm_mobile_base_control_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_base_m = 0.5f;
    axis.config.wheel_radius_m = 0.1f;
    axis.config.max_wheel_m_s = 2.0f;
    axis.resources.write_wheels = write_wheels;

    TEST_ASSERT_EQUAL(BM_OK, bm_mobile_base_control_init(&axis));

    /* 纯直行：左右轮速均为 1.0 m/s */
    bm_mobile_base_control_set_cmd(&axis, 1.0f, 0.0f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, g_right);

    /* 原地转：wheel_base=0.5 → left=-0.25, right=+0.25 */
    bm_mobile_base_control_set_cmd(&axis, 0.0f, 1.0f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.25f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f,  0.25f, g_right);
}

/* ------------------------------------------------------------------ */
/* 测试用例 2：坡道前馈场景                                               */
/* slope_angle=30°(π/6), gain=1.0 → ff = sin(π/6)*9.81 = 4.905 m/s   */
/* v_cmd=0 → left=right=-4.905（被限幅到 ±2.0）                          */
/* 本测试验证前馈已生效（轮速 != 0 且符号正确）                             */
/* ------------------------------------------------------------------ */
void test_mobile_base_slope_feedforward(void) {
    bm_mobile_base_control_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_base_m = 0.5f;
    axis.config.wheel_radius_m = 0.1f;
    axis.config.max_wheel_m_s = 5.0f;
    axis.config.enable_slope_feedforward = 1;
    axis.config.slope_angle_rad = 3.14159265f / 6.0f; /* 30° */
    axis.config.slope_feedforward_gain = 1.0f;
    axis.resources.write_wheels = write_wheels;

    TEST_ASSERT_EQUAL(BM_OK, bm_mobile_base_control_init(&axis));

    /* 指令为 0，但有坡道前馈叠加 */
    bm_mobile_base_control_set_cmd(&axis, 0.0f, 0.0f);
    bm_mobile_base_control_step(&axis);

    /* ff = sin(30°)*9.81 ≈ 4.905 m/s，左右轮速应接近 4.905 */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.905f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.905f, g_right);

    /* 验证遥测中 slope_feedforward_m_s 非零 */
    TEST_ASSERT_TRUE(axis.state.telemetry.slope_feedforward_m_s > 0.0f);
}

/* ------------------------------------------------------------------ */
/* 测试用例 3：差速运动学边界——超速时应限幅                                */
/* v=3.0, ω=0 → 原始 left=right=3.0，超出 max_wheel_m_s=2.0，应被截断    */
/* ------------------------------------------------------------------ */
void test_mobile_base_wheel_clamp_boundary(void) {
    bm_mobile_base_control_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_base_m = 0.5f;
    axis.config.wheel_radius_m = 0.1f;
    axis.config.max_wheel_m_s = 2.0f;
    axis.resources.write_wheels = write_wheels;

    TEST_ASSERT_EQUAL(BM_OK, bm_mobile_base_control_init(&axis));

    /* 超速指令：3.0 > max_wheel_m_s=2.0 */
    bm_mobile_base_control_set_cmd(&axis, 3.0f, 0.0f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, g_right);

    /* 负方向超速 */
    bm_mobile_base_control_set_cmd(&axis, -3.0f, 0.0f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.0f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.0f, g_right);
}

/* ------------------------------------------------------------------ */
/* 测试用例 4：exec_ops 生命周期（init/start/safe_stop）                  */
/* ------------------------------------------------------------------ */
void test_mobile_base_exec_ops_lifecycle(void) {
    static bm_mobile_base_control_axis_t axis;
    bm_exec_t inst;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_base_m = 0.5f;
    axis.config.wheel_radius_m = 0.1f;
    axis.config.max_wheel_m_s = 2.0f;
    axis.resources.write_wheels = write_wheels;

    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    /* exec_init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_mobile_base_control_exec_ops.init(&inst));

    /* exec_start 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_mobile_base_control_exec_ops.start(&inst));

    /* 设置指令并走一步 */
    bm_mobile_base_control_set_cmd(&axis, 1.5f, 0.5f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_TRUE(g_left != 0.0f || g_right != 0.0f);

    /* safe_stop 后轮速应写零 */
    bm_mobile_base_control_exec_ops.safe_stop(&inst);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, g_right);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, axis.state.linear_cmd_m_s);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, axis.state.angular_cmd_rad_s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mobile_base_differential_kinematics);
    RUN_TEST(test_mobile_base_slope_feedforward);
    RUN_TEST(test_mobile_base_wheel_clamp_boundary);
    RUN_TEST(test_mobile_base_exec_ops_lifecycle);
    return UNITY_END();
}

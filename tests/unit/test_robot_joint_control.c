/**
 * @file test_robot_joint_control.c
 * @brief robot_joint_control 组件单元测试
 *
 * 覆盖：PI 力矩输出基础行为、超速限幅、关节软限位边界、
 * exec_ops 生命周期（safe_stop 归零）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补超速限幅 / 软限位边界 / exec_ops 测试用例
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/robot_joint_control.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static float g_pos;
static float g_vel;
static float g_torque;

static int read_joint(void *user, float *position_rad, float *velocity_rad_s) {
    (void)user;
    *position_rad = g_pos;
    *velocity_rad_s = g_vel;
    return 0;
}

static int write_torque(void *user, float torque_nm) {
    (void)user;
    g_torque = torque_nm;
    return 0;
}

/** 构造一个有效的默认轴（kp=2, ki=0, setpoint=1.0, vel_max=5, torque_max=5, dt=0.01）*/
static void make_default_axis(bm_robot_joint_control_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.pi.kp = 2.0f;
    axis->config.pi.ki = 0.0f;
    axis->config.pi.out_min = -10.0f;
    axis->config.pi.out_max = 10.0f;
    axis->config.pi.integrator_min = -10.0f;
    axis->config.pi.integrator_max = 10.0f;
    axis->config.position_setpoint_rad = 1.0f;
    axis->config.position_min_rad = -3.14f;
    axis->config.position_max_rad = 3.14f;
    axis->config.velocity_max_rad_s = 5.0f;
    axis->config.torque_max_nm = 5.0f;
    axis->config.dt_s = 0.01f;
    axis->resources.read_joint = read_joint;
    axis->resources.write_torque = write_torque;
}

void setUp(void) {
    g_pos = 0.0f;
    g_vel = 0.0f;
    g_torque = 0.0f;
}

void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* 测试用例 1：PI 正向驱动力矩（位置误差 = 1.0 → 力矩应 > 0 且 ≤ 5 N·m）  */
/* ------------------------------------------------------------------ */
void test_robot_joint_pi_drives_torque(void) {
    bm_robot_joint_control_axis_t axis;

    make_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_robot_joint_control_init(&axis));

    /* pos=0, setpoint=1.0 → err=1.0 → torque = kp*err = 2.0 */
    bm_robot_joint_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, g_torque);
    TEST_ASSERT_TRUE(g_torque > 0.0f);
    TEST_ASSERT_TRUE(g_torque <= 5.0f);
}

/* ------------------------------------------------------------------ */
/* 测试用例 2：超速限幅——关节速度超出 velocity_max 时应被截断              */
/* vel=10 > velocity_max=5 → 限幅为 5，内部计算摩擦前馈基于限幅后速度       */
/* 以 kp=2 err=0（setpoint=pos）验证力矩仅来自摩擦前馈部分                  */
/* ------------------------------------------------------------------ */
void test_robot_joint_velocity_overspeed_clamp(void) {
    bm_robot_joint_control_axis_t axis;

    make_default_axis(&axis);
    /* 让 PI 误差为零，纯看速度限幅后的摩擦前馈量 */
    axis.config.position_setpoint_rad = 0.0f;
    /* 设置库仑摩擦，无死区，粘性为 0 */
    axis.config.coulomb_friction = 1.0f;
    axis.config.viscous_friction = 0.0f;
    axis.config.friction_deadband = 0.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_robot_joint_control_init(&axis));

    /* 超速情形：vel=10 应被截断为 5 */
    g_vel = 10.0f;
    g_pos = 0.0f;
    bm_robot_joint_control_step(&axis);

    /* 验证 telemetry 中记录的速度已被限幅 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, axis.state.telemetry.velocity_rad_s);

    /* 负超速：vel=-10 → 限幅为 -5 */
    g_vel = -10.0f;
    bm_robot_joint_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -5.0f, axis.state.telemetry.velocity_rad_s);
}

/* ------------------------------------------------------------------ */
/* 测试用例 3：关节软限位边界——setpoint 超出 [min,max] 时应被夹紧到边界      */
/* setpoint=5.0 > position_max=3.14 → 实际误差 = 3.14-0=3.14            */
/* kp=2 → torque = 2*3.14 = 6.28，被 torque_max=5 限幅到 5.0            */
/* ------------------------------------------------------------------ */
void test_robot_joint_soft_limit_clamp(void) {
    bm_robot_joint_control_axis_t axis;

    make_default_axis(&axis);
    axis.config.position_setpoint_rad = 5.0f;   /* 超出软限位 */
    axis.config.position_max_rad = 3.14f;
    axis.config.torque_max_nm = 5.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_robot_joint_control_init(&axis));

    g_pos = 0.0f;
    g_vel = 0.0f;
    bm_robot_joint_control_step(&axis);

    /* torque = clamp(2*3.14, -5, 5) = 5.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, g_torque);
}

/* ------------------------------------------------------------------ */
/* 测试用例 4：exec_ops 生命周期（init/start/safe_stop 归零）              */
/* ------------------------------------------------------------------ */
void test_robot_joint_exec_ops_lifecycle(void) {
    static bm_robot_joint_control_axis_t axis;
    bm_exec_t inst;

    make_default_axis(&axis);

    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    /* exec_init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_robot_joint_control_exec_ops.init(&inst));

    /* exec_start 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_robot_joint_control_exec_ops.start(&inst));

    /* 走一步产生非零力矩 */
    g_pos = 0.0f;
    g_vel = 0.0f;
    bm_robot_joint_control_step(&axis);
    TEST_ASSERT_TRUE(g_torque != 0.0f);

    /* safe_stop 后力矩应为零，PI 积分清零 */
    bm_robot_joint_control_exec_ops.safe_stop(&inst);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, g_torque);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, axis.state.torque_cmd_nm);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_robot_joint_pi_drives_torque);
    RUN_TEST(test_robot_joint_velocity_overspeed_clamp);
    RUN_TEST(test_robot_joint_soft_limit_clamp);
    RUN_TEST(test_robot_joint_exec_ops_lifecycle);
    return UNITY_END();
}

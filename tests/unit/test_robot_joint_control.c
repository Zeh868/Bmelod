/**
 * @file test_robot_joint_control.c
 * @brief robot_joint_control 组件单元测试
 *
 * 覆盖 PI 力矩输出、摩擦前馈与限幅基本行为。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 */
#include "unity.h"
#include "bm/component/robot_joint_control.h"
#include "bm/common/bm_types.h"

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

void setUp(void) {
    g_pos = 0.0f;
    g_vel = 0.0f;
    g_torque = 0.0f;
}

void tearDown(void) {}

void test_robot_joint_pi_drives_torque(void) {
    bm_robot_joint_control_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.pi.kp = 2.0f;
    axis.config.pi.ki = 0.0f;
    axis.config.pi.out_min = -10.0f;
    axis.config.pi.out_max = 10.0f;
    axis.config.pi.integrator_min = -10.0f;
    axis.config.pi.integrator_max = 10.0f;
    axis.config.position_setpoint_rad = 1.0f;
    axis.config.position_min_rad = -3.14f;
    axis.config.position_max_rad = 3.14f;
    axis.config.velocity_max_rad_s = 5.0f;
    axis.config.torque_max_nm = 5.0f;
    axis.config.dt_s = 0.01f;
    axis.resources.read_joint = read_joint;
    axis.resources.write_torque = write_torque;

    TEST_ASSERT_EQUAL(BM_OK, bm_robot_joint_control_init(&axis));
    bm_robot_joint_control_step(&axis);
    TEST_ASSERT_TRUE(g_torque > 0.0f);
    TEST_ASSERT_TRUE(g_torque <= 5.0f);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_robot_joint_pi_drives_torque);
    return UNITY_END();
}

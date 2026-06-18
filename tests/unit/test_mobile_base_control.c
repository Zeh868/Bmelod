/**
 * @file test_mobile_base_control.c
 * @brief mobile_base_control 组件单元测试
 *
 * 覆盖差速运动学 v,ω → 左右轮速换算。
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

void test_mobile_base_differential_kinematics(void) {
    bm_mobile_base_control_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_base_m = 0.5f;
    axis.config.wheel_radius_m = 0.1f;
    axis.config.max_wheel_m_s = 2.0f;
    axis.resources.write_wheels = write_wheels;

    TEST_ASSERT_EQUAL(BM_OK, bm_mobile_base_control_init(&axis));
    bm_mobile_base_control_set_cmd(&axis, 1.0f, 0.0f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, g_right);

    bm_mobile_base_control_set_cmd(&axis, 0.0f, 1.0f);
    bm_mobile_base_control_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.25f, g_left);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, g_right);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mobile_base_differential_kinematics);
    return UNITY_END();
}

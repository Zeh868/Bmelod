/**
 * @file test_control_loop.c
 * @brief control_loop 串级 PI 组件单元测试
 *
 * 覆盖外环设定、内环跟踪与输出饱和基本行为。
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
#include "bm/component/control_loop.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static float g_outer_meas;
static float g_inner_meas;
static float g_setpoint;
static float g_last_output;

static int read_plant(void *user, float *outer_meas, float *inner_meas,
                      float *setpoint) {
    (void)user;
    *outer_meas = g_outer_meas;
    *inner_meas = g_inner_meas;
    *setpoint = g_setpoint;
    return 0;
}

static int write_output(void *user, float output) {
    (void)user;
    g_last_output = output;
    g_inner_meas += (output - g_inner_meas) * 0.2f;
    g_outer_meas += (g_inner_meas - g_outer_meas) * 0.1f;
    return 0;
}

void setUp(void) {
    g_outer_meas = 0.0f;
    g_inner_meas = 0.0f;
    g_setpoint = 1.0f;
    g_last_output = 0.0f;
}

void tearDown(void) {
}

static void test_validate_config_rejects_bad_dt(void) {
    bm_control_loop_config_t cfg = { .dt_s = 0.0f };
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_control_loop_validate_config(&cfg));
}

static void test_cascade_pi_moves_toward_setpoint(void) {
    bm_control_loop_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.dt_s = 0.01f;
    axis.config.outer_pi.kp = 2.0f;
    axis.config.outer_pi.ki = 5.0f;
    axis.config.outer_pi.out_min = -10.0f;
    axis.config.outer_pi.out_max = 10.0f;
    axis.config.outer_pi.integrator_min = -20.0f;
    axis.config.outer_pi.integrator_max = 20.0f;
    axis.config.inner_pi.kp = 3.0f;
    axis.config.inner_pi.ki = 8.0f;
    axis.config.inner_pi.out_min = -5.0f;
    axis.config.inner_pi.out_max = 5.0f;
    axis.config.inner_pi.integrator_min = -10.0f;
    axis.config.inner_pi.integrator_max = 10.0f;
    axis.resources.read_plant = read_plant;
    axis.resources.write_output = write_output;

    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_validate_config(&axis.config));
    bm_control_loop_reset(&axis);

    for (i = 0u; i < 500u; ++i) {
        bm_control_loop_step(&axis);
    }
    TEST_ASSERT_TRUE(g_outer_meas > 0.3f);
    TEST_ASSERT_TRUE(fabsf(g_last_output) <= 5.0f + 0.001f);
    TEST_ASSERT_TRUE(axis.state.step_count == 500u);
}

void test_control_loop(void) {
    RUN_TEST(test_validate_config_rejects_bad_dt);
    RUN_TEST(test_cascade_pi_moves_toward_setpoint);
}

int main(void) {
    UNITY_BEGIN();
    test_control_loop();
    return UNITY_END();
}

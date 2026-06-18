/**
 * @file test_power_control.c
 * @brief power_control 组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm/component/power_control.h"

#include <string.h>

#define PLANT_V_TAU_S   0.02f
#define PLANT_I_TAU_S   0.01f
#define PLANT_V_GAIN    1.0f
#define PLANT_I_GAIN    2.0f

static float g_plant_v;
static float g_plant_i;
static float g_last_duty;
static float g_max_duty;
static uint32_t g_tel_count;

static int read_fb(void *user, float *v_out_v, float *i_out_a) {
    (void)user;
    *v_out_v = g_plant_v;
    *i_out_a = g_plant_i;
    return 0;
}

static int write_duty(void *user, float duty) {
    float v_target;
    float i_target;

    (void)user;
    g_last_duty = duty;
    if (duty > g_max_duty) {
        g_max_duty = duty;
    }

    v_target = duty * PLANT_V_GAIN;
    i_target = duty * PLANT_I_GAIN;
    g_plant_v += (v_target - g_plant_v) * (0.001f / PLANT_V_TAU_S);
    g_plant_i += (i_target - g_plant_i) * (0.001f / PLANT_I_TAU_S);
    return 0;
}

static void publish_tel(void *user, const bm_power_ctrl_telemetry_t *telemetry) {
    (void)user;
    (void)telemetry;
    g_tel_count++;
}

void setUp(void) {
    g_plant_v = 0.0f;
    g_plant_i = 0.0f;
    g_last_duty = 0.0f;
    g_max_duty = 0.0f;
    g_tel_count = 0u;
}

void tearDown(void) {
}

void test_power_control_tracks_voltage_setpoint(void) {
    bm_power_control_axis_t axis;
    bm_power_ctrl_cmd_t cmd;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.pi_voltage.kp = 2.0f;
    axis.config.pi_voltage.ki = 10.0f;
    axis.config.pi_voltage.out_min = -5.0f;
    axis.config.pi_voltage.out_max = 5.0f;
    axis.config.pi_voltage.integrator_min = -10.0f;
    axis.config.pi_voltage.integrator_max = 10.0f;
    axis.config.pi_current.kp = 1.0f;
    axis.config.pi_current.ki = 20.0f;
    axis.config.pi_current.out_min = 0.0f;
    axis.config.pi_current.out_max = 1.0f;
    axis.config.pi_current.integrator_min = -2.0f;
    axis.config.pi_current.integrator_max = 2.0f;
    axis.config.v_ramp.rate_per_s = 5.0f;
    axis.config.i_limit_a = 5.0f;
    axis.config.duty_min = 0.0f;
    axis.config.duty_max = 1.0f;
    axis.config.voltage_dt_s = 0.01f;
    axis.config.current_dt_s = 0.001f;
    axis.resources.read_feedback = read_fb;
    axis.resources.write_duty = write_duty;
    axis.resources.publish_telemetry = publish_tel;

    TEST_ASSERT_EQUAL(BM_OK, bm_power_control_validate_config(&axis.config));
    bm_power_control_reset(&axis);

    cmd.sequence = 1u;
    cmd.status = BM_POWER_CTRL_CMD_ENABLED;
    cmd.v_set_v = 1.0f;
    bm_power_control_apply_command(&axis, &cmd);

    for (i = 0u; i < 500u; ++i) {
        bm_power_control_voltage_step(&axis);
        bm_power_control_current_step(&axis);
    }

    TEST_ASSERT_TRUE(g_plant_v > 0.2f);
    TEST_ASSERT_TRUE(g_max_duty > 0.0f);
    TEST_ASSERT_TRUE(g_tel_count > 0u);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_power_control_tracks_voltage_setpoint);
    return UNITY_END();
}

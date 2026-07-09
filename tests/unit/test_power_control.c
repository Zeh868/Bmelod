/**
 * @file test_power_control.c
 * @brief power_control 组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-09
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-07-09       1.1            zeh            补缺口 12 回归：write_duty 失败须
 *                                                当拍发布 FAULT 遥测
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
static bm_power_ctrl_telemetry_t g_last_telemetry;

static int read_fb(void *user, float *v_out_v, float *i_out_a) {
    (void)user;
    *v_out_v = g_plant_v;
    *i_out_a = g_plant_i;
    return 0;
}

/** H13：模拟电流采样失败（返回非零），v_out/i_out 均写 0 模拟"读不到" */
static int read_fb_fail(void *user, float *v_out_v, float *i_out_a) {
    (void)user;
    *v_out_v = 0.0f;
    *i_out_a = 0.0f;
    return -1;
}

/** 缺口 12：模拟 write_duty 写入失败（返回非零） */
static int write_duty_fail(void *user, float duty) {
    (void)user;
    (void)duty;
    return -1;
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
    g_last_telemetry = *telemetry;
    g_tel_count++;
}

void setUp(void) {
    g_plant_v = 0.0f;
    g_plant_i = 0.0f;
    g_last_duty = 0.0f;
    g_max_duty = 0.0f;
    g_tel_count = 0u;
    memset(&g_last_telemetry, 0, sizeof(g_last_telemetry));
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

/**
 * @brief H13 回归：current_step 中 read_feedback 失败时须锁存故障、
 *        输出安全占空比 duty_min，不得以 i_out=0 喂 PI 施加错误大修正
 */
void test_power_control_current_step_latches_fault_on_feedback_failure(void) {
    bm_power_control_axis_t axis;
    bm_power_ctrl_cmd_t cmd;

    memset(&axis, 0, sizeof(axis));
    axis.config.pi_current.kp = 1.0f;
    axis.config.pi_current.ki = 20.0f;
    axis.config.pi_current.out_min = 0.0f;
    axis.config.pi_current.out_max = 1.0f;
    axis.config.pi_current.integrator_min = -2.0f;
    axis.config.pi_current.integrator_max = 2.0f;
    axis.config.duty_min = 0.0f;
    axis.config.duty_max = 1.0f;
    axis.config.current_dt_s = 0.001f;
    axis.resources.read_feedback = read_fb_fail;
    axis.resources.write_duty = write_duty;
    axis.resources.publish_telemetry = publish_tel;

    bm_power_control_reset(&axis);

    cmd.sequence = 1u;
    cmd.status = BM_POWER_CTRL_CMD_ENABLED;
    cmd.v_set_v = 0.0f;
    bm_power_control_apply_command(&axis, &cmd);

    /* 模拟电压环已算出较大参考电流，验证不会被误用来施加大修正 */
    axis.state.i_ref_a = 5.0f;

    bm_power_control_current_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, axis.config.duty_min, axis.state.duty);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, axis.config.duty_min, g_last_duty);
}

/**
 * @brief 缺口 12 回归：current_step 中 write_duty 失败时须锁存故障且仍发布遥测
 *        （带 BM_POWER_CTRL_TEL_FAULT 位），不得因当拍直接 return 而丢发遥测，
 *        导致 FAULT 状态要等下一拍才对上层可见。
 */
void test_power_control_current_step_publishes_fault_telemetry_on_write_duty_failure(void) {
    bm_power_control_axis_t axis;
    bm_power_ctrl_cmd_t cmd;
    uint32_t tel_before;

    memset(&axis, 0, sizeof(axis));
    axis.config.pi_current.kp = 1.0f;
    axis.config.pi_current.ki = 20.0f;
    axis.config.pi_current.out_min = 0.0f;
    axis.config.pi_current.out_max = 1.0f;
    axis.config.pi_current.integrator_min = -2.0f;
    axis.config.pi_current.integrator_max = 2.0f;
    axis.config.duty_min = 0.0f;
    axis.config.duty_max = 1.0f;
    axis.config.current_dt_s = 0.001f;
    axis.resources.read_feedback = read_fb;
    axis.resources.write_duty = write_duty_fail;
    axis.resources.publish_telemetry = publish_tel;

    bm_power_control_reset(&axis);

    cmd.sequence = 1u;
    cmd.status = BM_POWER_CTRL_CMD_ENABLED;
    cmd.v_set_v = 0.0f;
    bm_power_control_apply_command(&axis, &cmd);
    axis.state.i_ref_a = 1.0f;

    tel_before = g_tel_count;
    bm_power_control_current_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    /* 遥测须当拍就发布出去（不能等下一拍），且携带 FAULT 位。 */
    TEST_ASSERT_TRUE(g_tel_count > tel_before);
    TEST_ASSERT_BITS(BM_POWER_CTRL_TEL_FAULT,
                     BM_POWER_CTRL_TEL_FAULT,
                     g_last_telemetry.status);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_power_control_tracks_voltage_setpoint);
    RUN_TEST(test_power_control_current_step_latches_fault_on_feedback_failure);
    RUN_TEST(test_power_control_current_step_publishes_fault_telemetry_on_write_duty_failure);
    return UNITY_END();
}

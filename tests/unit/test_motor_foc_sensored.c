/**
 * @file test_motor_foc_sensored.c
 * @brief Tests for the sensored FOC motor component.
 */
#include "unity.h"

#include "bm/component/motor_foc_sensored.h"
#include "bm_hal_encoder_sim.h"
#include "bm_hal_pwm_sim.h"

#include <string.h>

static float s_id_feedback;
static float s_iq_feedback;
static float s_last_theta;
static bm_motor_foc_cmd_t s_callback_command;
static bm_motor_foc_telemetry_t s_callback_telemetry;
static uint32_t s_telemetry_publish_count;

void setUp(void) {
    s_id_feedback = 0.0f;
    s_iq_feedback = 0.0f;
    s_last_theta = 0.0f;
    memset(&s_callback_command, 0, sizeof(s_callback_command));
    memset(&s_callback_telemetry, 0, sizeof(s_callback_telemetry));
    s_telemetry_publish_count = 0u;
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
    bm_hal_encoder_sim_set_count(&BM_HAL_ENC_SIM0, 0);
}

void tearDown(void) {}

static bm_motor_foc_sensored_axis_t make_axis(void) {
    bm_motor_foc_sensored_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config = (bm_motor_foc_sensored_config_t){
        .pole_pairs = 4.0f,
        .encoder_direction = 1.0f,
        .electrical_offset_rad = 0.0f,
        .vbus_v = 24.0f,
        .phase_r_ohm = 0.5f,
        .v_max_pu = 0.95f,
        .current_dt_s = 0.0001f,
        .speed_dt_s = 0.001f,
        .iq_max_a = 2.0f,
        .pi_d = {
            .kp = 0.1f, .ki = 1.0f,
            .out_min = -0.95f, .out_max = 0.95f,
            .integrator_min = -1.0f, .integrator_max = 1.0f
        },
        .pi_q = {
            .kp = 0.1f, .ki = 1.0f,
            .out_min = -0.95f, .out_max = 0.95f,
            .integrator_min = -1.0f, .integrator_max = 1.0f
        },
        .pi_speed = {
            .kp = 0.1f, .ki = 1.0f,
            .out_min = -2.0f, .out_max = 2.0f,
            .integrator_min = -2.0f, .integrator_max = 2.0f
        },
        .speed_ramp = { .rate_per_s = 100.0f },
        .encoder = { .counts_per_rev = 4096u }
    };
    axis.resources.pwm = &BM_HAL_PWM_SIM0;
    axis.resources.encoder = &BM_HAL_ENC_SIM0;
    axis.resources.pwm_max = 1000u;
    axis.resources.current_adc_scale = 1000.0f;
    return axis;
}

static void capture_voltage(void *user, float vd, float vq, float theta) {
    (void)user;
    (void)vd;
    (void)vq;
    s_last_theta = theta;
}

static int read_callback_command(void *user, bm_motor_foc_cmd_t *command) {
    (void)user;
    *command = s_callback_command;
    return 0;
}

static void capture_telemetry(
    void *user,
    const bm_motor_foc_telemetry_t *telemetry) {
    (void)user;
    s_callback_telemetry = *telemetry;
    s_telemetry_publish_count++;
}

static void test_adc_failure_latches_fault_and_stops_pwm(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1));

    bm_motor_foc_sensored_current_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.fault_count);
    TEST_ASSERT_BITS(BM_MOTOR_FOC_TEL_FAULT,
                     BM_MOTOR_FOC_TEL_FAULT,
                     axis.state.telemetry.status);
    TEST_ASSERT_FALSE(bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
}

static void test_encoder_failure_latches_fault_and_stops_pwm(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.resources.encoder = NULL;
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1));

    bm_motor_foc_sensored_speed_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.fault_count);
    TEST_ASSERT_FALSE(bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
}

static void test_encoder_direction_and_electrical_offset_are_applied(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.config.encoder_direction = -1.0f;
    axis.config.electrical_offset_rad = 0.25f;
    axis.resources.sim_fb.id_a = &s_id_feedback;
    axis.resources.sim_fb.iq_a = &s_iq_feedback;
    axis.resources.on_voltage = capture_voltage;
    axis.state.speed.encoder.position_rad = 0.5f;
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;

    bm_motor_foc_sensored_current_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.75f, s_last_theta);
}

static void test_speed_step_preserves_current_pi_integrator(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    axis.state.cmd.speed_setpoint_rad_s = 5.0f;
    axis.state.current.pi_q.integrator = 0.5f;

    bm_motor_foc_sensored_speed_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 0.5f, axis.state.current.pi_q.integrator);
}

static void test_exec_callbacks_exchange_command_and_telemetry(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();
    bm_exec_t instance;

    memset(&instance, 0, sizeof(instance));
    instance.state = &axis;
    axis.resources.sim_fb.id_a = &s_id_feedback;
    axis.resources.sim_fb.iq_a = &s_iq_feedback;
    axis.resources.read_command = read_callback_command;
    axis.resources.publish_telemetry = capture_telemetry;
    s_callback_command.sequence = 7u;
    s_callback_command.status = BM_MOTOR_FOC_CMD_ENABLED;

    bm_motor_foc_sensored_exec_current(&instance);

    TEST_ASSERT_EQUAL_UINT32(7u, axis.state.cmd.sequence);
    TEST_ASSERT_EQUAL_UINT32(1u, s_telemetry_publish_count);
    TEST_ASSERT_BITS(BM_MOTOR_FOC_TEL_VALID,
                     BM_MOTOR_FOC_TEL_VALID,
                     s_callback_telemetry.status);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_adc_failure_latches_fault_and_stops_pwm);
    RUN_TEST(test_encoder_failure_latches_fault_and_stops_pwm);
    RUN_TEST(test_encoder_direction_and_electrical_offset_are_applied);
    RUN_TEST(test_speed_step_preserves_current_pi_integrator);
    RUN_TEST(test_exec_callbacks_exchange_command_and_telemetry);
    return UNITY_END();
}

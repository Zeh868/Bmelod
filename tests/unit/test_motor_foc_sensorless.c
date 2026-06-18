/**
 * @file test_motor_foc_sensorless.c
 * @brief motor_foc_sensorless 组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */
#include "unity.h"
#include "bm/component/motor_foc_sensorless.h"

#include <math.h>
#include <string.h>

static float g_sim_id;
static float g_sim_iq;
static float g_sim_theta;

static int read_cmd(void *user, bm_motor_sl_cmd_t *command) {
    (void)user;
    command->sequence = 1u;
    command->status = BM_MOTOR_SL_CMD_ENABLED;
    command->iq_ref_a = 1.0f;
    return 0;
}

static void publish_tel(void *user, const bm_motor_sl_telemetry_t *telemetry) {
    (void)user;
    (void)telemetry;
}

static void init_axis(bm_motor_foc_sensorless_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.pole_pairs = 4.0f;
    axis->config.vbus_v = 24.0f;
    axis->config.phase_r_ohm = 0.5f;
    axis->config.ld_h = 0.001f;
    axis->config.lq_h = 0.002f;
    axis->config.psi_f_wb = 0.05f;
    axis->config.v_max_pu = 0.95f;
    axis->config.current_dt_s = 0.0001f;
    axis->config.iq_max_a = 5.0f;
    axis->config.enable_mtpa = 1;
    axis->config.enable_fw = 1;
    axis->config.pi_d = (bm_algo_pi_config_t){
        .kp = 0.5f, .ki = 50.0f, .out_min = -1.0f, .out_max = 1.0f,
        .integrator_min = -2.0f, .integrator_max = 2.0f
    };
    axis->config.pi_q = axis->config.pi_d;
    axis->config.observer = (bm_algo_flux_observer_config_t){
        .rs_ohm = 0.5f, .ls_h = 0.001f, .pll_kp = 20.0f, .pll_ki = 200.0f
    };
    axis->config.align_time_s = 0.001f;
    axis->config.align_id_a = 0.2f;
    axis->config.open_loop_omega_start = 50.0f;
    axis->config.open_loop_omega_end = 100.0f;
    axis->config.open_loop_ramp_s = 0.01f;
    axis->config.observer_lock_omega_rad_s = 1.0f;
    axis->config.observer_lock_time_s = 10.0f;
    axis->resources.sim_fb.id_a = &g_sim_id;
    axis->resources.sim_fb.iq_a = &g_sim_iq;
    axis->resources.sim_fb.theta_elec_rad = &g_sim_theta;
    axis->resources.read_command = read_cmd;
    axis->resources.publish_telemetry = publish_tel;
}

void setUp(void) {
    g_sim_id = 0.0f;
    g_sim_iq = 0.0f;
    g_sim_theta = 0.0f;
}

void tearDown(void) {
}

void test_motor_foc_sensorless_runs_current_loop(void) {
    bm_motor_foc_sensorless_axis_t axis;
    uint32_t i;

    init_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_motor_foc_sensorless_validate_config(&axis.config));
    bm_motor_foc_sensorless_reset(&axis);
    axis.state.cmd.status = BM_MOTOR_SL_CMD_ENABLED;
    axis.state.cmd.iq_ref_a = 1.0f;

    for (i = 0u; i < 200u; ++i) {
        g_sim_theta += 0.01f;
        g_sim_iq += 0.001f;
        bm_motor_foc_sensorless_current_step(&axis);
    }

    TEST_ASSERT_TRUE(axis.state.loop_count >= 200u);
    TEST_ASSERT_TRUE(fabsf(axis.state.telemetry.iq_ref_a) > 0.5f);
    TEST_ASSERT_TRUE(axis.state.fault_latched == 0);
    TEST_ASSERT_EQUAL(BM_MOTOR_SL_PHASE_OBSERVER, axis.state.phase);
}

void test_motor_foc_sensorless_phase_transitions(void) {
    bm_motor_foc_sensorless_axis_t axis;
    uint32_t i;
    int saw_align = 0;
    int saw_open = 0;
    int saw_observer = 0;

    init_axis(&axis);
    bm_motor_foc_sensorless_reset(&axis);
    bm_motor_foc_sensorless_apply_command(&axis, &(bm_motor_sl_cmd_t){
        .status = BM_MOTOR_SL_CMD_ENABLED,
        .iq_ref_a = 1.0f
    });

    for (i = 0u; i < 300u; ++i) {
        g_sim_theta += 0.05f;
        bm_motor_foc_sensorless_current_step(&axis);
        if (axis.state.phase == BM_MOTOR_SL_PHASE_ALIGN) {
            saw_align = 1;
        }
        if (axis.state.phase == BM_MOTOR_SL_PHASE_OPEN_LOOP) {
            saw_open = 1;
        }
        if (axis.state.phase == BM_MOTOR_SL_PHASE_OBSERVER) {
            saw_observer = 1;
            break;
        }
    }

    TEST_ASSERT_TRUE(saw_align != 0);
    TEST_ASSERT_TRUE(saw_open != 0);
    TEST_ASSERT_TRUE(saw_observer != 0);
}

void test_motor_foc_sensorless_sim_uses_feedback_theta(void) {
    bm_motor_foc_sensorless_axis_t axis;

    init_axis(&axis);
    bm_motor_foc_sensorless_reset(&axis);
    axis.state.cmd.status = BM_MOTOR_SL_CMD_ENABLED;
    axis.state.cmd.iq_ref_a = 1.0f;
    axis.state.phase = BM_MOTOR_SL_PHASE_OBSERVER;

    g_sim_theta = 1.23f;
    g_sim_id = 0.1f;
    g_sim_iq = 0.2f;
    bm_motor_foc_sensorless_current_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.23f,
                             axis.state.telemetry.theta_elec_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, axis.state.telemetry.id_meas_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, axis.state.telemetry.iq_meas_a);
}

void test_motor_foc_sensorless_rejects_invalid_observer(void) {
    bm_motor_foc_sensorless_axis_t axis;

    init_axis(&axis);
    axis.config.observer.ls_h = 0.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_motor_foc_sensorless_validate_config(&axis.config));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motor_foc_sensorless_runs_current_loop);
    RUN_TEST(test_motor_foc_sensorless_sim_uses_feedback_theta);
    RUN_TEST(test_motor_foc_sensorless_phase_transitions);
    RUN_TEST(test_motor_foc_sensorless_rejects_invalid_observer);
    return UNITY_END();
}

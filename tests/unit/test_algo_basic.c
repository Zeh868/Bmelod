/**
 * @file test_algo_basic.c
 * @brief bm_algorithm 基础工具/PI/运动轮廓单元测试
 *
 * 由 test_algorithm.c 按域拆分而来（架构改进计划任务 1.5b 项 6），纯移动、
 * 不改测试内容；测试用例总数与拆分前的 Unity 内部计数之和保持不变。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       1.0            zeh            自 test_algorithm.c 拆分
 *
 */

#include "unity.h"
#include "bm_algorithm.h"

#include <math.h>
#include <string.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

static void test_common_clamp_and_deadband(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, bm_algo_clamp_f(10.0f, 0.0f, 5.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, bm_algo_deadband_f(0.05f, 0.1f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, bm_algo_deadband_f(0.2f, 0.1f));
}

static void test_pi_step_and_saturation(void) {
    bm_algo_pi_config_t cfg = {
        .kp = 1.0f,
        .ki = 10.0f,
        .out_min = -1.0f,
        .out_max = 1.0f,
        .integrator_min = -10.0f,
        .integrator_max = 10.0f
    };
    bm_algo_pi_state_t st;
    float out;
    int i;

    bm_algo_pi_reset(&st, 0.0f);
    for (i = 0; i < 100; ++i) {
        out = bm_algo_pi_step(&st, &cfg, 1.0f, 0.001f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, out);
}

static void test_ramp_reaches_target(void) {
    bm_algo_ramp_config_t cfg = { .rate_per_s = 10.0f };
    bm_algo_ramp_state_t st;
    float v;
    int i;

    bm_algo_ramp_reset(&st, 0.0f);
    for (i = 0; i < 200; ++i) {
        v = bm_algo_ramp_step(&st, &cfg, 1.0f, 0.01f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, v);
    TEST_ASSERT_EQUAL(1, st.done);
}

static void test_scurve_reaches_target(void) {
    bm_algo_scurve_config_t cfg = {
        .max_vel = 1.0f,
        .max_accel = 2.0f,
        .max_jerk = 10.0f
    };
    bm_algo_scurve_state_t st;
    int i;

    bm_algo_scurve_reset(&st, 0.0f, 0.0f, 0.0f);
    bm_algo_scurve_set_target(&st, 1.0f);
    for (i = 0; i < 1000 && !st.done; ++i) {
        (void)bm_algo_scurve_step(&st, &cfg, 0.01f);
    }

    TEST_ASSERT_EQUAL(1, st.done);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, st.position);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, st.velocity);
}

static void test_motion_and_profile_boundary_regressions(void) {
    bm_algo_encoder_config_t enc_cfg = { .counts_per_rev = 4096u };
    bm_algo_encoder_state_t enc;
    bm_algo_dda_config_t dda_cfg = {
        .x0 = 0.0f, .y0 = 0.0f, .x1 = 0.5f, .y1 = 0.0f,
        .step_size = 0.1f
    };
    bm_algo_dda_state_t dda;
    bm_algo_trapezoid_config_t trap_cfg = {
        .max_vel = 1.0f, .max_accel = 2.0f, .max_decel = 2.0f
    };
    bm_algo_trapezoid_state_t trap;
    float x = 0.0f;
    float y = 0.0f;
    int steps = 0;

    bm_algo_encoder_reset(&enc, &enc_cfg, 2048);
    (void)bm_algo_encoder_update(&enc, &enc_cfg, 2048, 0.001f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159265f, enc.position_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, enc.velocity_rad_s);

    bm_algo_dda_reset(&dda, &dda_cfg);
    while (bm_algo_dda_step(&dda, &dda_cfg, &x, &y) != 0) {
        steps++;
        TEST_ASSERT_TRUE(steps <= 6);
    }
    TEST_ASSERT_EQUAL_INT(5, steps);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, y);

    bm_algo_trapezoid_reset(&trap, 0.0f, 0.0f);
    bm_algo_trapezoid_set_target(&trap, 1.0f);
    (void)bm_algo_trapezoid_step(&trap, &trap_cfg, 2.0f);
    TEST_ASSERT_EQUAL(1, trap.done);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, trap.position);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, trap.velocity);
}

static void test_numeric_guard_regressions(void) {
    bm_algo_agc_config_t agc_cfg = {
        .target_level = 1.0f,
        .attack_coeff = 1.0f,
        .release_coeff = 1.0f,
        .gain = 1.0f,
        .min_gain = 0.1f,
        .max_gain = 4.0f,
        .silence_threshold = 0.001f
    };
    bm_algo_agc_state_t agc;
    bm_algo_kalman1d_config_t kalman_cfg = { .q = 0.0f, .r = 0.0f };
    bm_algo_kalman1d_state_t kalman;
    bm_algo_stats_state_t stats;
    float silence[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float quiet[1] = { 0.01f };
    float out[4];
    int i;

    TEST_ASSERT_TRUE(isinf(bm_algo_angle_wrap_rad(INFINITY)));
    TEST_ASSERT_TRUE(isinf(bm_algo_angle_wrap_0_2pi_rad(-INFINITY)));

    bm_algo_agc_reset(&agc, 2.0f);
    bm_algo_agc_process(&agc, &agc_cfg, silence, out, 4u);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, agc.gain);
    for (i = 0; i < 4; ++i) {
        bm_algo_agc_process(&agc, &agc_cfg, quiet, out, 1u);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 4.0f, agc.gain);

    bm_algo_kalman1d_reset(&kalman, 3.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 3.0f,
        bm_algo_kalman1d_update(&kalman, &kalman_cfg, 100.0f));
    TEST_ASSERT_TRUE(isfinite(kalman.x));

    TEST_ASSERT_EQUAL_INT8(INT8_MAX,
                           bm_algo_quantize_f32_to_i8(INFINITY, 0.1f, 0));
    TEST_ASSERT_EQUAL_INT8(INT8_MIN,
                           bm_algo_quantize_f32_to_i8(-INFINITY, 0.1f, 0));
    TEST_ASSERT_EQUAL_INT8(0,
                           bm_algo_quantize_f32_to_i8(NAN, 0.1f, 0));

    bm_algo_stats_reset(&stats);
    bm_algo_stats_push(&stats, 100000.0f);
    bm_algo_stats_push(&stats, 100001.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f,
                             bm_algo_stats_variance(&stats));
}

static void test_runtime_buffer_config_changes_are_rejected(void) {
    float moving_buffer[3] = { 0.0f, 0.0f, 1234.0f };
    bm_algo_moving_avg_config_t moving_cfg = {
        .buffer = moving_buffer, .length = 2u
    };
    bm_algo_moving_avg_state_t moving;
    float rms_buffer[3] = { 0.0f, 0.0f, 1234.0f };
    bm_algo_rms_config_t rms_cfg = { .window_samples = 2u };
    bm_algo_rms_state_t rms;
    const float fir_coeffs[3] = { 1.0f, 0.0f, 0.0f };
    float fir_delay[3] = { 0.0f, 0.0f, 1234.0f };
    bm_algo_fir_config_t fir_cfg = {
        .coeffs = fir_coeffs, .tap_count = 2u, .delay_line = fir_delay
    };
    bm_algo_fir_state_t fir;
    const float poly_coeffs[3] = { 1.0f, 0.0f, 0.0f };
    float poly_delay[3] = { 0.0f, 0.0f, 1234.0f };
    bm_algo_polyphase_decim_config_t poly_cfg = {
        .coeffs = poly_coeffs, .tap_count = 2u, .decim = 2u
    };
    bm_algo_polyphase_decim_state_t poly;
    const float input = 1.0f;
    float output = 0.0f;

    TEST_ASSERT_EQUAL(0, bm_algo_moving_avg_init(&moving, &moving_cfg));
    moving_cfg.length = 3u;
    TEST_ASSERT_FLOAT_WITHIN(
        0.0f, input, bm_algo_moving_avg_step(&moving, &moving_cfg, input));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1234.0f, moving_buffer[2]);

    TEST_ASSERT_EQUAL(0, bm_algo_rms_init(&rms, &rms_cfg, rms_buffer, 2u));
    rms_cfg.window_samples = 3u;
    TEST_ASSERT_FLOAT_WITHIN(
        0.0f, input, bm_algo_rms_step(&rms, &rms_cfg, input));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1234.0f, rms_buffer[2]);

    TEST_ASSERT_EQUAL(0, bm_algo_fir_init(&fir, &fir_cfg));
    fir_cfg.tap_count = 3u;
    TEST_ASSERT_FLOAT_WITHIN(
        0.0f, input, bm_algo_fir_step(&fir, &fir_cfg, input));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1234.0f, fir_delay[2]);

    TEST_ASSERT_EQUAL(
        0, bm_algo_polyphase_decim_init(&poly, &poly_cfg, poly_delay, 2u));
    poly_cfg.tap_count = 3u;
    TEST_ASSERT_EQUAL_UINT32(
        0u,
        bm_algo_polyphase_decim_process(
            &poly, &poly_cfg, &input, &output, 1u));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1234.0f, poly_delay[2]);
}

void test_algo_basic(void) {
    RUN_TEST(test_common_clamp_and_deadband);
    RUN_TEST(test_pi_step_and_saturation);
    RUN_TEST(test_ramp_reaches_target);
    RUN_TEST(test_scurve_reaches_target);
    RUN_TEST(test_motion_and_profile_boundary_regressions);
    RUN_TEST(test_numeric_guard_regressions);
    RUN_TEST(test_runtime_buffer_config_changes_are_rejected);
}

int main(void) {
    UNITY_BEGIN();
    test_algo_basic();
    return UNITY_END();
}

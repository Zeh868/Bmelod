/**
 * @file test_algo_misc.c
 * @brief bm_algorithm 图像/视觉/通信/音频/杂项定点单元测试
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

static void test_coulomb_soc(void) {
    bm_algo_coulomb_config_t cfg = {
        .nominal_capacity_ah = 10.0f,
        .coulomb_efficiency = 1.0f,
        .soc_min = 0.0f,
        .soc_max = 1.0f
    };
    bm_algo_coulomb_state_t st;
    float soc;

    bm_algo_coulomb_reset(&st, 0.5f);
    soc = bm_algo_coulomb_step(&st, &cfg, 1.0f, 3600.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.6f, soc);
}

static void test_image_label_merges_connected_pixels(void) {
    const uint8_t binary[12] = {
        1u, 1u, 0u, 0u,
        0u, 1u, 0u, 0u,
        0u, 0u, 0u, 1u
    };
    uint16_t labels[12];
    bm_algo_blob_info_t blobs[2];
    int count;

    count = bm_algo_image_label_u8(binary, labels, 4u, 3u, blobs, 2u);

    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL(labels[0], labels[1]);
    TEST_ASSERT_EQUAL(labels[0], labels[5]);
    TEST_ASSERT_NOT_EQUAL(labels[0], labels[11]);
    TEST_ASSERT_EQUAL_UINT32(3u, blobs[0].area);
    TEST_ASSERT_EQUAL_UINT32(1u, blobs[1].area);
}

static void test_fixed_pi_q31_saturates(void) {
    bm_algo_pi_q31_config_t cfg = {
        .kp = bm_algo_float_to_q31(0.8f),
        .ki = bm_algo_float_to_q31(0.5f),
        .out_min = bm_algo_float_to_q31(-1.0f),
        .out_max = bm_algo_float_to_q31(1.0f),
        .integrator_min = bm_algo_float_to_q31(-1.0f),
        .integrator_max = bm_algo_float_to_q31(1.0f)
    };
    bm_algo_pi_q31_state_t st;
    bm_algo_q31_t out;
    int i;

    bm_algo_pi_q31_reset(&st, 0);
    for (i = 0; i < 300; ++i) {
        out = bm_algo_pi_q31_step(&st, &cfg,
                                  bm_algo_float_to_q31(1.0f),
                                  bm_algo_float_to_q31(0.01f));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.08f, 1.0f, bm_algo_q31_to_float(out));
}

static void test_fixed_lpf1_q15_tracks_input(void) {
    bm_algo_lpf1_q15_config_t cfg = {
        .alpha_q15 = bm_algo_float_to_q15(0.1f)
    };
    bm_algo_lpf1_q15_state_t st;
    bm_algo_q15_t v = 0;
    int i;

    bm_algo_lpf1_q15_reset(&st, 0);
    for (i = 0; i < 500; ++i) {
        v = bm_algo_lpf1_q15_step(&st, &cfg, BM_ALGO_Q15_ONE);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, bm_algo_q15_to_float(v));
}

static void test_fixed_point_saturates_before_narrowing(void) {
    bm_algo_biquad_q15_config_t bq_cfg = {
        .b0 = BM_ALGO_Q15_ONE,
        .b1 = 0,
        .b2 = 0,
        .a1 = 0,
        .a2 = 0
    };
    bm_algo_biquad_q15_state_t bq = {
        .z1 = BM_ALGO_Q15_ONE,
        .z2 = 0
    };
    bm_algo_pi_q31_config_t pi_cfg = {
        .kp = 0,
        .ki = 0,
        .out_min = (bm_algo_q31_t)INT32_MIN,
        .out_max = BM_ALGO_Q31_ONE,
        .integrator_min = (bm_algo_q31_t)INT32_MIN,
        .integrator_max = BM_ALGO_Q31_ONE
    };
    bm_algo_pi_q31_state_t pi = {
        .integrator = BM_ALGO_Q31_ONE - 1,
        .output = 0
    };

    TEST_ASSERT_EQUAL_INT16(
        BM_ALGO_Q15_ONE,
        bm_algo_biquad_q15_step(&bq, &bq_cfg, BM_ALGO_Q15_ONE));
    (void)bm_algo_pi_q31_step(
        &pi, &pi_cfg, BM_ALGO_Q31_ONE, bm_algo_float_to_q31(0.5f));
    TEST_ASSERT_EQUAL_INT32(BM_ALGO_Q31_ONE, pi.integrator);
}

static void test_fixed_point_negative_full_scale_product_saturates(void) {
    bm_algo_pi_q31_config_t pi_cfg = {
        .kp = (bm_algo_q31_t)INT32_MIN,
        .ki = 0,
        .out_min = (bm_algo_q31_t)INT32_MIN,
        .out_max = BM_ALGO_Q31_ONE,
        .integrator_min = (bm_algo_q31_t)INT32_MIN,
        .integrator_max = BM_ALGO_Q31_ONE
    };
    bm_algo_pi_q31_state_t pi;
    bm_algo_biquad_q15_config_t bq_cfg = {
        .b0 = (bm_algo_q15_t)INT16_MIN,
        .b1 = 0,
        .b2 = 0,
        .a1 = 0,
        .a2 = 0
    };
    bm_algo_biquad_q15_state_t bq;

    bm_algo_pi_q31_reset(&pi, 0);
    TEST_ASSERT_EQUAL_INT32(
        BM_ALGO_Q31_ONE,
        bm_algo_pi_q31_step(&pi, &pi_cfg, (bm_algo_q31_t)INT32_MIN, 1));

    bm_algo_biquad_q15_reset(&bq);
    TEST_ASSERT_EQUAL_INT16(
        BM_ALGO_Q15_ONE,
        bm_algo_biquad_q15_step(
            &bq, &bq_cfg, (bm_algo_q15_t)INT16_MIN));
}

static void test_soc_and_image_boundary_regressions(void) {
    bm_algo_soc_ekf_config_t ekf_cfg = {
        .q_soc = 0.0f,
        .q_bias = 0.0f,
        .r_v = 0.01f,
        .coulomb_efficiency = 1.0f,
        .nominal_capacity_ah = 10.0f,
        .ocv_slope_v_per_soc = 0.5f
    };
    bm_algo_soc_ekf_state_t ekf;
    const uint8_t src[9] = {
        255u, 255u, 255u,
        255u, 255u, 255u,
        255u, 255u, 255u
    };
    uint8_t dst[9];

    bm_algo_soc_ekf_reset(&ekf, 0.8f);
    bm_algo_soc_ekf_update_voltage(&ekf, &ekf_cfg, 3.9f, 3.9f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.8f, ekf.soc);

    memset(dst, 0xA5, sizeof(dst));
    bm_algo_image_erode_u8(src, dst, 3u, 3u);
    TEST_ASSERT_EQUAL_UINT8(0u, dst[0]);
    TEST_ASSERT_EQUAL_UINT8(255u, dst[4]);
    TEST_ASSERT_EQUAL_UINT8(0u, dst[8]);
}

static void test_battery_temp_and_motor_extras(void) {
    bm_algo_battery_temp_config_t temp_cfg = {
        .ref_temp_c = 25.0f,
        .capacity_coeff_per_c = 0.01f,
        .ocv_shift_v_per_c = 0.002f
    };
    bm_algo_abc_t abc;
    bm_algo_rate_est_state_t rate;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.1f,
        bm_algo_battery_temp_capacity_ah(1.0f, 35.0f, &temp_cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.68f,
        bm_algo_battery_temp_compensate_ocv(3.7f, 35.0f, &temp_cfg));

    bm_algo_current_from_2shunt(1.0f, -0.5f, &abc);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, abc.ic);

    bm_algo_rate_est_reset(&rate, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f,
        bm_algo_rate_est_step(&rate, 1.0f, 0.1f));
}

static void test_zero_length_audio_is_ignored(void) {
    bm_algo_agc_config_t agc_cfg = {
        .target_level = 1.0f,
        .attack_coeff = 1.0f,
        .release_coeff = 1.0f,
        .gain = 1.0f
    };
    bm_algo_vad_config_t vad_cfg = {
        .energy_threshold = 0.1f,
        .alpha = 1.0f
    };
    bm_algo_agc_state_t agc;
    bm_algo_vad_state_t vad;
    float sample = 1.0f;

    bm_algo_agc_reset(&agc, 2.0f);
    bm_algo_vad_reset(&vad);
    bm_algo_agc_process(&agc, &agc_cfg, &sample, &sample, 0u);
    bm_algo_vad_process(&vad, &vad_cfg, &sample, 0u);

    TEST_ASSERT_FLOAT_WITHIN(0.0f, 2.0f, agc.gain);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, vad.energy);
}

static void test_vision_centroid_and_compensation(void) {
    const uint8_t mask[9] = {
        0u, 1u, 0u,
        1u, 1u, 0u,
        0u, 0u, 0u
    };
    float cx = 0.0f;
    float cy = 0.0f;

    TEST_ASSERT_EQUAL(0, bm_algo_vision_centroid_u8(mask, 3u, 3u, &cx, &cy));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.667f, cx);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.667f, cy);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
        bm_algo_deadzone_inverse(0.05f, 0.1f, 2.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f,
        bm_algo_deadzone_inverse(0.2f, 0.1f, 2.0f));
}

static void test_p1_k0_and_fixed_extensions(void) {
    bm_algo_redundant_pair_config_t rp_cfg = {
        .tolerance_abs = 0.01f,
        .tolerance_rel = 0.0f
    };
    bm_algo_dob_config_t dob_cfg = { .plant_gain = 2.0f, .lpf_alpha = 0.5f };
    bm_algo_dob_state_t dob;
    bm_algo_svpwm_out_t pwm;
    bm_algo_order_tracker_config_t ot_cfg = {
        .sample_hz = 1000.0f,
        .pole_pairs = 2.0f,
        .lpf_alpha = 0.5f
    };
    bm_algo_order_tracker_state_t ot;
    bm_algo_integrator_q31_config_t int_cfg = {
        .min = bm_algo_float_to_q31(-1.0f),
        .max = bm_algo_float_to_q31(1.0f)
    };
    bm_algo_integrator_q31_state_t int_st;
    bm_algo_rate_limit_q31_config_t rl_cfg = {
        .max_rise_per_s_q31 = bm_algo_float_to_q31(1.0f),
        .max_fall_per_s_q31 = bm_algo_float_to_q31(1.0f)
    };
    bm_algo_rate_limit_q31_state_t rl_st;
    bm_algo_pr_q15_config_t pr_cfg = {
        .b0 = bm_algo_float_to_q15(0.5f),
        .b1 = 0,
        .b2 = 0,
        .a1 = 0,
        .a2 = 0,
        .out_min = (bm_algo_q15_t)(-32768),
        .out_max = BM_ALGO_Q15_ONE
    };
    bm_algo_pr_q15_state_t pr_st;
    float disturbance;
    bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(0.01f);

    TEST_ASSERT_EQUAL(0u, bm_algo_redundant_pair_step(1.0f, 1.0f, &rp_cfg));
    TEST_ASSERT_EQUAL(BM_ALGO_FAULT_REDUNDANT_MISMATCH,
                      bm_algo_redundant_pair_step(1.0f, 2.0f, &rp_cfg));

    bm_algo_dob_reset(&dob);
    (void)bm_algo_dob_step(&dob, &dob_cfg, 1.0f, 2.5f, &disturbance);
    TEST_ASSERT_TRUE(fabsf(disturbance) > 0.0f);

    bm_algo_svpwm_overmod(10.0f, 0.0f, 24.0f, 0.577f, &pwm);
    TEST_ASSERT_TRUE(pwm.duty_a >= 0.0f && pwm.duty_a <= 1.0f);

    bm_algo_order_tracker_reset(&ot);
    bm_algo_order_tracker_feed(&ot, &ot_cfg, 3000.0f, 100.0f);
    TEST_ASSERT_TRUE(ot.filtered_order > 0.0f);
    TEST_ASSERT_TRUE(ot.shaft_hz > 0.0f);

    bm_algo_integrator_q31_reset(&int_st, 0);
    TEST_ASSERT_TRUE(bm_algo_integrator_q31_step(&int_st, &int_cfg,
        bm_algo_float_to_q31(1.0f), dt_q31) > 0);

    bm_algo_rate_limit_q31_reset(&rl_st, 0);
    TEST_ASSERT_TRUE(bm_algo_rate_limit_q31_step(&rl_st, &rl_cfg,
        bm_algo_float_to_q31(1.0f), dt_q31) >= 0);

    TEST_ASSERT_EQUAL(0, bm_algo_deadband_q31(bm_algo_float_to_q31(0.01f),
                                              bm_algo_float_to_q31(0.05f)));

    bm_algo_pr_q15_reset(&pr_st);
    TEST_ASSERT_TRUE(bm_algo_pr_q15_step(&pr_st, &pr_cfg,
        bm_algo_float_to_q15(0.5f)) != 0);
}

static void test_detection_matched_and_ultrasonic(void) {
    const float sig[8] = { 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 0.0f, 0.0f, 0.0f };
    const float tmpl[3] = { 1.0f, 2.0f, 1.0f };
    uint32_t idx = 0u;
    float echo[32];
    int32_t tof;
    int i;

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f,
        bm_algo_matched_filter(sig, 8u, tmpl, 3u, &idx));
    TEST_ASSERT_EQUAL_UINT32(2u, idx);

    for (i = 0; i < 32; ++i) {
        echo[i] = 0.0f;
    }
    echo[10] = 1.0f;
    echo[11] = 0.8f;
    tof = bm_algo_ultrasonic_tof(echo, 32u, 4u, 0.3f, 0.5f);
    TEST_ASSERT_EQUAL_INT32(10, tof);
}

static void test_w2_audio_spectral_motion(void) {
    bm_algo_eq_peaking_config_t eq_cfg = {
        .sample_hz = 48000.0f,
        .freq_hz = 1000.0f,
        .q = 1.0f,
        .gain_db = 6.0f
    };
    bm_algo_eq_peaking_state_t eq;
    float in[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
    float out[4];
    float win[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float mag[4];
    bm_algo_stepper_config_t st_cfg = { .max_velocity_steps_s = 1000.0f };
    bm_algo_stepper_state_t st;
    int8_t pulses[4];

    TEST_ASSERT_EQUAL(0, bm_algo_eq_peaking_design(&eq, &eq_cfg));
    bm_algo_eq_peaking_process(&eq, &eq_cfg, in, out, 4u);
    TEST_ASSERT_TRUE(fabsf(out[0]) > 0.0f);

    TEST_ASSERT_EQUAL(0, bm_algo_stft_magnitude_frame(in, win, 4u, mag));
    TEST_ASSERT_TRUE(mag[0] >= 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f,
        bm_algo_order_from_hz(50.0f, 3000.0f, 1u));

    bm_algo_stepper_reset(&st, 0);
    TEST_ASSERT_TRUE(bm_algo_stepper_process(&st, &st_cfg, 100.0f, 0.01f,
                                             pulses, 4u) > 0u);
}

static void test_review_fixes(void) {
    bm_algo_eq_peaking_config_t bad_eq = { .sample_hz = 0.0f };
    bm_algo_eq_peaking_state_t eq;
    float in[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float out[4];
    float ref[64];
    float sig[64];
    float gcc_work[512];
    bm_algo_smith_predictor_config_t smith_cfg = {
        .model_gain = 1.0f,
        .delay_steps = 2u
    };
    float delay_line[2];
    bm_algo_smith_predictor_state_t smith;
    const uint8_t src[9] = { 0u };
    int16_t gx[9];
    int16_t gy[9];
    int i;

    memset(&eq, 0, sizeof(eq));
    bm_algo_eq_peaking_process(&eq, &bad_eq, in, out, 4u);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, out[3]);

    for (i = 0; i < 64; ++i) {
        ref[i] = 0.0f;
        sig[i] = 0.0f;
    }
    ref[10] = 1.0f;
    sig[13] = 1.0f;
    TEST_ASSERT_EQUAL_UINT32(512u, bm_algo_gcc_phat_work_count(64u, 10));
    TEST_ASSERT_EQUAL_INT32(3, bm_algo_gcc_phat_delay(ref, sig, 64u, 10,
                                                      gcc_work, 512u));
    TEST_ASSERT_EQUAL_INT32(BM_ALGO_GCC_PHAT_DELAY_INVALID,
                            bm_algo_gcc_phat_delay(NULL, sig, 64u, 10,
                                                   gcc_work, 512u));

    TEST_ASSERT_EQUAL(0, bm_algo_smith_predictor_init(&smith, &smith_cfg,
                                                      delay_line, 2u));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f,
        bm_algo_smith_predictor_step(&smith, &smith_cfg, 5.0f, 2.0f, 1.0f));

    memset(gx, 0x5A, sizeof(gx));
    memset(gy, 0x5A, sizeof(gy));
    bm_algo_vision_sobel_u8(src, gx, gy, 3u, 3u);
    TEST_ASSERT_EQUAL_INT16(0, gx[0]);
    TEST_ASSERT_EQUAL_INT16(0, gy[8]);
}

void test_algo_misc(void) {
    RUN_TEST(test_coulomb_soc);
    RUN_TEST(test_image_label_merges_connected_pixels);
    RUN_TEST(test_fixed_pi_q31_saturates);
    RUN_TEST(test_fixed_lpf1_q15_tracks_input);
    RUN_TEST(test_fixed_point_saturates_before_narrowing);
    RUN_TEST(test_fixed_point_negative_full_scale_product_saturates);
    RUN_TEST(test_soc_and_image_boundary_regressions);
    RUN_TEST(test_battery_temp_and_motor_extras);
    RUN_TEST(test_zero_length_audio_is_ignored);
    RUN_TEST(test_vision_centroid_and_compensation);
    RUN_TEST(test_p1_k0_and_fixed_extensions);
    RUN_TEST(test_detection_matched_and_ultrasonic);
    RUN_TEST(test_w2_audio_spectral_motion);
    RUN_TEST(test_review_fixes);
}

int main(void) {
    UNITY_BEGIN();
    test_algo_misc();
    return UNITY_END();
}

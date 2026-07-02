/**
 * @file test_algorithm.c
 * @brief bm_algorithm 算法库单元测试
 *
 * 覆盖公共工具、PI、滤波、斜坡、电机变换、FFT 与电池算法基本行为。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            第四批 PDM/MFCC/RGB/定点
 * 2026-06-17       1.2            zeh            第五批 delay-and-sum/resize/定点4
 * 2026-06-17       1.3            zeh            第六批定点5与黄金向量
 * 2026-06-17       1.4            zeh            第七批 MVDR/定点6/参考向量
 * 2026-06-17       1.5            zeh            第八批定点7/黄金向量/TinyML
 * 2026-06-17       1.6            zeh            第九批定点8/黄金向量/TinyML ADD
 * 2026-06-17       1.7            zeh            第十批定点9/DOB/包络RMS Q31/背隙
 * 2026-06-17       1.8            zeh            第十一批定点10/互补/前馈/黄金向量
 * 2026-06-17       1.9            zeh            第十二批定点11/PI/梯形/冗余/速率/SOC
 * 2026-06-17       2.0            zeh            第十三批定点12/MPPT/S曲线/信号质量
 * 2026-06-17       2.1            zeh            第十四批定点全族 Q15/Q31 收口
 */
#include "unity.h"
#include "bm_algorithm.h"
#include "../reference_vectors/ref_lpf1_q31.h"
#include "../reference_vectors/ref_mvdr_2ch.h"
#include "../reference_vectors/ref_rms_q15.h"
#include "../reference_vectors/ref_trapezoid_q31.h"
#include "../reference_vectors/ref_coulomb_q31.h"
#include "../reference_vectors/ref_integrator_q15.h"
#include "../reference_vectors/ref_lead_lag_q15.h"
#include "../reference_vectors/ref_dob_q15.h"
#include "../reference_vectors/ref_biquad_q31.h"
#include "../reference_vectors/ref_differentiator_q15.h"
#include "../reference_vectors/ref_coulomb_q15.h"
#include "../reference_vectors/ref_pi_q15.h"
#include "../reference_vectors/ref_trapezoid_q15.h"
#include "../reference_vectors/ref_mppt_po_q15.h"
#include "../reference_vectors/ref_scurve_q15.h"
#include "../reference_vectors/ref_median_q15.h"
#include "../reference_vectors/ref_pid2_q15.h"
#include "../reference_vectors/ref_mppt_po_q31.h"

#include <math.h>
#include <string.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

static const bm_algo_goertzel_config_t s_readonly_goertzel_config = {
    .target_freq_hz = 100.0f,
    .sample_hz = 1000.0f,
    .block_size = 20u,
    .coeff = 0.0f
};

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

static void test_lpf1_step(void) {
    bm_algo_lpf1_config_t cfg;
    bm_algo_lpf1_state_t st;
    float v = 0.0f;
    int i;

    TEST_ASSERT_EQUAL(0, bm_algo_lpf1_init_from_cutoff(&cfg, 10.0f, 1000.0f));
    bm_algo_lpf1_reset(&st, 0.0f);
    for (i = 0; i < 500; ++i) {
        v = bm_algo_lpf1_step(&st, &cfg, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, v);
}

static void test_hpf1_uses_high_pass_coefficient(void) {
    bm_algo_hpf1_config_t cfg;
    bm_algo_hpf1_state_t st;
    float first;
    float settled = 0.0f;
    int i;

    TEST_ASSERT_EQUAL(0, bm_algo_hpf1_init_from_cutoff(&cfg, 10.0f, 1000.0f));
    bm_algo_hpf1_reset(&st);
    first = bm_algo_hpf1_step(&st, &cfg, 1.0f);
    for (i = 0; i < 200; ++i) {
        settled = bm_algo_hpf1_step(&st, &cfg, 1.0f);
    }

    TEST_ASSERT_TRUE(first > 0.9f);
    TEST_ASSERT_TRUE(fabsf(settled) < 0.001f);
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

static void test_clarke_park_roundtrip(void) {
    bm_algo_abc_t abc = { .ia = 1.0f, .ib = -0.5f, .ic = -0.5f };
    bm_algo_alphabeta_t ab;
    bm_algo_dq_t dq;
    bm_algo_alphabeta_t ab2;

    bm_algo_clarke(&abc, &ab);
    bm_algo_park(&ab, 0.5f, &dq);
    bm_algo_inv_park(&dq, 0.5f, &ab2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ab.i_alpha, ab2.i_alpha);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ab.i_beta, ab2.i_beta);
}

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

static void test_rfft_execute(void) {
    float time_data[BM_ALGO_FFT_SIZE_64];
    float work[BM_ALGO_FFT_SIZE_64 * 2u];
    float spectrum[BM_ALGO_FFT_SIZE_64 / 2u + 1u];
    bm_algo_rfft_f32_t fft;
    uint32_t i;

    for (i = 0u; i < BM_ALGO_FFT_SIZE_64; ++i) {
        time_data[i] = sinf(2.0f * 3.14159265f * 4.0f * (float)i / (float)BM_ALGO_FFT_SIZE_64);
    }

    TEST_ASSERT_EQUAL(0, bm_algo_rfft_f32_init(&fft, BM_ALGO_FFT_SIZE_64, work, (uint32_t)(sizeof(work) / sizeof(work[0]))));
    TEST_ASSERT_EQUAL(0, bm_algo_rfft_f32_execute(&fft, time_data, spectrum));
    TEST_ASSERT_TRUE(spectrum[4] > spectrum[3]);
    TEST_ASSERT_TRUE(spectrum[4] > spectrum[5]);
}

static void test_single_point_windows_are_finite(void) {
    float window;

    bm_algo_window_hann(&window, 1u);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1.0f, window);
    bm_algo_window_hamming(&window, 1u);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1.0f, window);
    bm_algo_window_blackman(&window, 1u);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 1.0f, window);
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

static void test_linear_resampler_ratio_and_capacity(void) {
    bm_algo_linear_resampler_state_t st;
    float outputs[3];
    uint32_t count;

    bm_algo_linear_resampler_reset(&st, 2.0f, 0.0f);
    TEST_ASSERT_EQUAL(2, bm_algo_linear_resampler_step(
        &st, 1.0f, outputs, 3u, &count));
    TEST_ASSERT_EQUAL_UINT32(2u, count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, outputs[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, outputs[1]);

    bm_algo_linear_resampler_reset(&st, 0.5f, 0.0f);
    TEST_ASSERT_EQUAL(0, bm_algo_linear_resampler_step(
        &st, 1.0f, outputs, 3u, &count));
    TEST_ASSERT_EQUAL(1, bm_algo_linear_resampler_step(
        &st, 2.0f, outputs, 3u, &count));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, outputs[0]);

    bm_algo_linear_resampler_reset(&st, 3.0f, 0.0f);
    TEST_ASSERT_EQUAL(-1, bm_algo_linear_resampler_step(
        &st, 1.0f, outputs, 2u, &count));
    TEST_ASSERT_EQUAL_UINT32(0u, count);
}

static void test_goertzel_accepts_readonly_config(void) {
    bm_algo_goertzel_state_t st;

    TEST_ASSERT_EQUAL(0, bm_algo_goertzel_init(
        &st, &s_readonly_goertzel_config));
    TEST_ASSERT_TRUE(fabsf(st.coeff) > 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0f, 0.0f, s_readonly_goertzel_config.coeff);
}

static void test_ekf_covariance_stays_symmetric(void) {
    bm_algo_ekf_cv_config_t cfg = {
        .q_pos = 0.01f,
        .q_vel = 0.01f,
        .r_pos = 0.1f
    };
    bm_algo_ekf_cv_state_t st;
    int i;

    bm_algo_ekf_cv_reset(&st, 0.0f, 0.0f);
    for (i = 0; i < 20; ++i) {
        bm_algo_ekf_cv_predict(&st, &cfg, 0.1f);
        bm_algo_ekf_cv_update(&st, &cfg, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, st.p01, st.p10);
}

static void test_ukf1d_identity_tracks_measurement(void) {
    bm_algo_ukf1d_config_t cfg = {
        .q = 0.01f,
        .r = 0.1f,
        .measurement_model = BM_ALGO_UKF1D_MEAS_IDENTITY
    };
    bm_algo_ukf1d_state_t st;
    int i;

    bm_algo_ukf1d_reset(&st, 0.0f, 1.0f);
    for (i = 0; i < 30; ++i) {
        bm_algo_ukf1d_predict(&st, &cfg);
        TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_OK,
                          bm_algo_ukf1d_update(&st, &cfg, 2.0f));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, st.x);
    TEST_ASSERT_TRUE(isfinite(st.p) && st.p >= 0.0f);
}

static void test_ukf1d_square_model_updates(void) {
    bm_algo_ukf1d_config_t cfg = {
        .q = 0.001f,
        .r = 0.05f,
        .measurement_model = BM_ALGO_UKF1D_MEAS_SQUARE
    };
    bm_algo_ukf1d_state_t st;

    bm_algo_ukf1d_reset(&st, 2.0f, 0.5f);
    bm_algo_ukf1d_predict(&st, &cfg);
    TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_OK,
                      bm_algo_ukf1d_update(&st, &cfg, 4.0f));
    TEST_ASSERT_TRUE(isfinite(st.x));
}

static void test_ekf_gate_and_gated_update(void) {
    bm_algo_ekf_cv_config_t cfg = {
        .q_pos = 0.01f,
        .q_vel = 0.01f,
        .r_pos = 0.1f
    };
    bm_algo_ekf_gate_config_t gate = { .innovation_threshold = 1.0f };
    bm_algo_ekf_cv_state_t st;
    float pos_before;

    TEST_ASSERT_EQUAL(1, bm_algo_ekf_gate_accept(0.1f, 1.0f, 9.0f));
    TEST_ASSERT_EQUAL(0, bm_algo_ekf_gate_accept(5.0f, 0.01f, 1.0f));

    bm_algo_ekf_cv_reset(&st, 0.0f, 0.0f);
    bm_algo_ekf_cv_predict(&st, &cfg, 0.1f);
    pos_before = st.pos;
    TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_GATED,
                      bm_algo_ekf_cv_update_gated(&st, &cfg, 100.0f, &gate));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, pos_before, st.pos);

    TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_OK,
                      bm_algo_ekf_cv_update_gated(&st, &cfg, 0.2f, &gate));
    TEST_ASSERT_TRUE(fabsf(st.pos - pos_before) > 0.0f);
}

static void test_imu_calib_apply_and_accumulator(void) {
    bm_algo_imu_calib_config_t calib = {
        .gyro_bias = { 0.1f, 0.0f, 0.0f },
        .accel_bias = { 0.0f, 0.0f, 0.2f },
        .gyro_scale = { 1.0f, 1.0f, 1.0f },
        .accel_scale = { 1.0f, 1.0f, 1.0f }
    };
    bm_algo_imu_calib_accumulator_t acc;
    const float raw_g[3] = { 0.2f, 0.0f, 0.0f };
    const float raw_a[3] = { 0.0f, 0.0f, 9.91f };
    const float expect_g[3] = { 0.0f, 0.0f, 9.81f };
    float out_g[3];
    float out_a[3];
    int i;

    bm_algo_imu_calib_apply(&calib, raw_g, raw_a, out_g, out_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, out_g[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.71f, out_a[2]);

    bm_algo_imu_calib_accumulator_reset(&acc);
    for (i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL(0, bm_algo_imu_calib_accumulator_feed(&acc, raw_g, raw_a));
    }
    memset(&calib, 0, sizeof(calib));
    TEST_ASSERT_EQUAL(0, bm_algo_imu_calib_accumulator_finish(&acc, expect_g, &calib));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, calib.gyro_bias[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.1f, calib.accel_bias[2]);
}

static void test_stft_overlap_emits_hop_frames(void) {
    bm_algo_stft_overlap_config_t cfg = {
        .frame_size = 8u,
        .hop_size = 4u,
        .window = NULL
    };
    bm_algo_stft_overlap_t st;
    float ring[8];
    float mag[8];
    uint32_t frames = 0u;
    int i;
    int rc;

    TEST_ASSERT_EQUAL(0, bm_algo_stft_overlap_init(&st, &cfg, ring, 8u));
    for (i = 0; i < 20; ++i) {
        float sample = (i < 8) ? 1.0f : 0.0f;
        rc = bm_algo_stft_overlap_feed(&st, &cfg, sample, mag, 8u);
        if (rc == 1) {
            frames++;
            TEST_ASSERT_TRUE(mag[0] >= 0.0f);
        } else {
            TEST_ASSERT_TRUE(rc == 0 || rc == -1);
        }
    }
    TEST_ASSERT_TRUE(frames >= 2u);
    TEST_ASSERT_TRUE(st.frame_count >= 2u);
}

static void test_mahony_uses_simultaneous_quaternion_update(void) {
    bm_algo_mahony_config_t cfg = { .kp = 0.0f, .ki = 0.0f };
    bm_algo_mahony_state_t st;
    float norm = sqrtf(1.14f);

    bm_algo_mahony_reset(&st);
    bm_algo_mahony_step(&st, &cfg, 1.0f, 2.0f, 3.0f,
                        0.0f, 0.0f, 0.0f, 0.2f);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f / norm, st.q.w);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1f / norm, st.q.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f / norm, st.q.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3f / norm, st.q.z);
}

static void test_sogi_states_decay_after_input_stops(void) {
    bm_algo_sogi_pll_config_t cfg = {
        .nominal_omega_rad_s = 2.0f * 3.14159265f * 50.0f,
        .k_sogi = 1.41421356f,
        .k_pll = 0.0f
    };
    bm_algo_sogi_pll_state_t st;
    int i;

    bm_algo_sogi_pll_reset(&st, &cfg);
    for (i = 0; i < 2000; ++i) {
        float input = sinf(cfg.nominal_omega_rad_s * (float)i * 0.0001f);
        bm_algo_sogi_pll_step(&st, &cfg, input, 0.0001f);
    }
    for (i = 0; i < 2000; ++i) {
        bm_algo_sogi_pll_step(&st, &cfg, 0.0f, 0.0001f);
    }

    TEST_ASSERT_TRUE(fabsf(st.v_alpha) < 0.001f);
    TEST_ASSERT_TRUE(fabsf(st.v_beta) < 0.001f);
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

static void test_motor_voltage_scaling_and_deadtime(void) {
    bm_algo_svpwm_out_t pwm;

    bm_algo_svpwm(6.0f, 0.0f, 24.0f, &pwm);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6875f, pwm.duty_a);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3125f, pwm.duty_b);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3125f, pwm.duty_c);

    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 3.2f,
        bm_algo_deadtime_comp_v_period(
            2.0f, 1.0f, 1e-6f, 20e-6f, 24.0f));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 2.0f,
        bm_algo_deadtime_comp_v(2.0f, 1.0f, 1e-6f, 24.0f));
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

static void test_flux_observer_and_mtpa(void) {
    bm_algo_flux_observer_config_t obs_cfg_ls = {
        .rs_ohm = 0.5f, .ls_h = 0.001f, .pll_kp = 10.0f, .pll_ki = 100.0f
    };
    bm_algo_flux_observer_config_t obs_cfg_no_ls = obs_cfg_ls;
    bm_algo_flux_observer_state_t obs_ls;
    bm_algo_flux_observer_state_t obs_no_ls;
    float theta_ls;
    float theta_no_ls;
    int i;

    obs_cfg_no_ls.ls_h = 0.0f;
    bm_algo_flux_observer_reset(&obs_ls, 0.0f);
    bm_algo_flux_observer_reset(&obs_no_ls, 0.0f);
    for (i = 0; i < 100; ++i) {
        float t = (float)i * 0.001f;
        float v_alpha = -sinf(t);
        float v_beta = cosf(t);
        float i_alpha = 0.5f * sinf(t);
        float i_beta = -0.5f * cosf(t);

        theta_ls = bm_algo_flux_observer_step(&obs_ls, &obs_cfg_ls,
                                              v_alpha, v_beta,
                                              i_alpha, i_beta, 0.001f);
        theta_no_ls = bm_algo_flux_observer_step(&obs_no_ls, &obs_cfg_no_ls,
                                                   v_alpha, v_beta,
                                                   i_alpha, i_beta, 0.001f);
        (void)theta_ls;
        (void)theta_no_ls;
    }
    TEST_ASSERT_TRUE(fabsf(obs_ls.omega_rad_s) > 0.1f);
    TEST_ASSERT_TRUE(fabsf(obs_ls.theta_rad - obs_no_ls.theta_rad) > 0.01f);
    TEST_ASSERT_TRUE(bm_algo_mtpa_id_ref(2.0f, 0.001f, 0.002f, 0.05f) < 0.0f);
    TEST_ASSERT_TRUE(
        fabsf(bm_algo_mtpa_id_ref(2.0f, 0.001f, 0.002f, 0.01f)) >
        fabsf(bm_algo_mtpa_id_ref(2.0f, 0.001f, 0.002f, 0.20f)));
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

static void test_soc_ekf_and_power_quality(void) {
    bm_algo_soc_ekf_config_t ekf_cfg = {
        .q_soc = 1e-6f,
        .q_bias = 1e-8f,
        .r_v = 0.01f,
        .coulomb_efficiency = 1.0f,
        .nominal_capacity_ah = 10.0f,
        .ocv_slope_v_per_soc = 0.5f
    };
    bm_algo_soc_ekf_state_t ekf;
    const float harmonics[3] = { 1.0f, 0.1f, 0.05f };
    float p;
    float q;
    float s;

    bm_algo_soc_ekf_reset(&ekf, 0.5f);
    bm_algo_soc_ekf_predict(&ekf, &ekf_cfg, 1.0f, 1.0f);
    bm_algo_soc_ekf_update_voltage(&ekf, &ekf_cfg, 3.7f, 3.65f);
    TEST_ASSERT_TRUE(ekf.soc >= 0.0f && ekf.soc <= 1.0f);
    TEST_ASSERT_TRUE(fabsf(ekf.p10) > 0.0f);
    TEST_ASSERT_TRUE(fabsf(ekf.bias_a) > 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.5f, 11.18f,
        bm_algo_thd_percent(harmonics, 3u));
    bm_algo_power_quality_pq(230.0f, 10.0f, 0.0f, &p, &q, &s);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2300.0f, p);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, q);
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

static void test_matched_filter_accepts_negative_correlations(void) {
    const float signal[2] = { -2.0f, -3.0f };
    const float template_data[1] = { 1.0f };
    uint32_t index = UINT32_MAX;

    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, -2.0f,
        bm_algo_matched_filter(signal, 2u, template_data, 1u, &index));
    TEST_ASSERT_EQUAL_UINT32(0u, index);
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

static void test_eq_stepper_and_smith_regressions(void) {
    bm_algo_eq_peaking_config_t eq_cfg = {
        .sample_hz = 48000.0f,
        .freq_hz = 1000.0f,
        .q = 1.0f,
        .gain_db = 0.0f
    };
    bm_algo_eq_peaking_state_t eq;
    const float impulse[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float eq_out[4];
    bm_algo_stepper_config_t step_cfg = {
        .max_velocity_steps_s = 1000.0f
    };
    bm_algo_stepper_state_t step;
    int8_t pulse;
    bm_algo_smith_predictor_config_t smith_cfg = {
        .model_gain = 1.0f,
        .delay_steps = 1u
    };
    bm_algo_smith_predictor_state_t smith;
    float delay_line[1];
    float low_measurement;
    float high_measurement;

    TEST_ASSERT_EQUAL(0, bm_algo_eq_peaking_design(&eq, &eq_cfg));
    bm_algo_eq_peaking_process(&eq, &eq_cfg, impulse, eq_out, 4u);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, eq_out[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, eq_out[1]);

    bm_algo_stepper_reset(&step, 0);
    TEST_ASSERT_EQUAL_UINT32(
        1u,
        bm_algo_stepper_process(
            &step, &step_cfg, 250.0f, 0.01f, &pulse, 1u));
    TEST_ASSERT_EQUAL_INT32(1, step.position_steps);
    TEST_ASSERT_TRUE(step.phase >= 1.0f);

    TEST_ASSERT_EQUAL(
        0,
        bm_algo_smith_predictor_init(
            &smith, &smith_cfg, delay_line, 1u));
    low_measurement = bm_algo_smith_predictor_step(
        &smith, &smith_cfg, 5.0f, 2.0f, 1.0f);
    bm_algo_smith_predictor_reset(&smith, &smith_cfg);
    high_measurement = bm_algo_smith_predictor_step(
        &smith, &smith_cfg, 5.0f, 3.0f, 1.0f);
    TEST_ASSERT_TRUE(high_measurement < low_measurement);
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

static void test_batch3_k0_extensions(void) {
    bm_algo_encoder_diag_config_t enc_diag_cfg = { .max_delta_per_step = 10 };
    bm_algo_encoder_diag_state_t enc_diag;
    bm_algo_backlash_state_t backlash;
    bm_algo_clock_drift_config_t drift_cfg = { .alpha = 0.5f };
    bm_algo_clock_drift_state_t drift;
    bm_algo_energy_wh_state_t wh;
    bm_algo_fsk2_config_t fsk_cfg = {
        .sample_hz = 8000.0f,
        .mark_hz = 1200.0f,
        .space_hz = 800.0f,
        .bit_rate_hz = 100.0f
    };
    bm_algo_biquad_config_t bq_cfg;
    bm_algo_biquad_state_t bq_st;
    bm_algo_ramp_q31_config_t ramp_cfg = {
        .rate_per_s_q31 = bm_algo_float_to_q31(10.0f)
    };
    bm_algo_ramp_q31_state_t ramp_st;
    float fsk_samples[80];
    uint8_t bits[4];
    uint32_t i;
    uint8_t code;
    uint8_t data = 0xAu;

    bm_algo_encoder_diag_reset(&enc_diag, 100);
    TEST_ASSERT_EQUAL(0u, bm_algo_encoder_diag_step(&enc_diag, &enc_diag_cfg,
                                                  105, 0));
    TEST_ASSERT_EQUAL(BM_ALGO_ENCODER_FAULT_MISSED,
                      bm_algo_encoder_diag_step(&enc_diag, &enc_diag_cfg,
                                                200, 1) &
                          BM_ALGO_ENCODER_FAULT_MISSED);
    TEST_ASSERT_NOT_EQUAL(0u, bm_algo_encoder_diag_step(&enc_diag, &enc_diag_cfg,
                                                        201, 1) &
                                  BM_ALGO_ENCODER_FAULT_INDEX);

    bm_algo_backlash_reset(&backlash);
    (void)bm_algo_backlash_inverse(1.0f, &backlash, 0.2f, 0.1f);
    TEST_ASSERT_TRUE(bm_algo_backlash_inverse(-1.0f, &backlash, 0.2f, 0.1f) <
                     -1.0f);

    bm_algo_clock_drift_reset(&drift);
    bm_algo_clock_drift_feed(&drift, &drift_cfg, 0.01f, 0.011f);
    TEST_ASSERT_TRUE(bm_algo_clock_drift_compensate(&drift, 0.011f) < 0.011f);

    bm_algo_energy_wh_reset(&wh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f / 3600.0f,
        bm_algo_energy_wh_integrator_step(&wh, 1000.0f, 1.0f));

    TEST_ASSERT_EQUAL_UINT32(0u, bm_algo_harmonic_group_index(0u, 10u, 2u));
    TEST_ASSERT_EQUAL_UINT32(1u, bm_algo_harmonic_group_index(25u, 10u, 2u));

    TEST_ASSERT_EQUAL(1, bm_algo_pwm_sample_window_valid(0u, 30.0f, 20.0f));
    TEST_ASSERT_EQUAL(0, bm_algo_pwm_sample_window_valid(0u, 90.0f, 10.0f));

    bm_algo_biquad_reset(&bq_st);
    TEST_ASSERT_EQUAL(0, bm_algo_biquad_notch_update(&bq_cfg, &bq_st,
        1000.0f, 50.0f, 5.0f));

    for (i = 0u; i < 80u; ++i) {
        fsk_samples[i] = sinf(2.0f * 3.14159265f * 1200.0f *
                              (float)i / 8000.0f);
    }
    TEST_ASSERT_TRUE(bm_algo_fsk2_demod_block(fsk_samples, 80u, &fsk_cfg,
                                              bits, 4u) >= 1);
    TEST_ASSERT_EQUAL_UINT8(1u, bits[0]);

    code = bm_algo_hamming74_encode(data);
    TEST_ASSERT_EQUAL_UINT8(data, bm_algo_hamming74_decode(code));
    code ^= (uint8_t)(1u << 3);
    TEST_ASSERT_EQUAL_UINT8(data, bm_algo_hamming74_decode(code));

    bm_algo_ramp_q31_reset(&ramp_st, 0);
    TEST_ASSERT_TRUE(bm_algo_ramp_q31_step(&ramp_st, &ramp_cfg,
        bm_algo_float_to_q31(1.0f), bm_algo_float_to_q31(0.1f)) > 0);
}

static void test_batch4_k0_and_fixed_batch3(void) {
    bm_algo_pdm_decimate_config_t pdm_cfg = {
        .decimation_factor = 4u,
        .gain = 1.0f
    };
    bm_algo_pdm_decimate_state_t pdm_st;
    int8_t pdm_in[16];
    float pcm_out[8];
    uint32_t n_out;
    uint32_t i;
    float mel[4];
    float dct[8] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f
    };
    float mfcc[2];
    bm_algo_mfcc_config_t mfcc_cfg = { .n_mfcc = 2u, .n_mels = 4u, .log_floor = 1e-6f };
    uint16_t rgb565[4] = { 0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu };
    uint8_t gray[4];
    uint8_t src8[16];
    uint8_t crop[4];
    bm_algo_image_crop_rect_t rect = { .x = 1u, .y = 1u, .width = 2u, .height = 2u };
    bm_algo_moving_avg_q15_config_t ma_cfg = { .window_size = 4u };
    bm_algo_moving_avg_q15_state_t ma_st;
    bm_algo_hysteresis_q31_config_t hys_cfg = {
        .low_threshold = bm_algo_float_to_q31(0.2f),
        .high_threshold = bm_algo_float_to_q31(0.5f)
    };
    bm_algo_hysteresis_q31_state_t hys_st;

    for (i = 0u; i < 16u; ++i) {
        pdm_in[i] = (int8_t)((i & 1u) ? 1 : -1);
    }
    bm_algo_pdm_decimate_reset(&pdm_st);
    n_out = bm_algo_pdm_decimate_block(&pdm_st, &pdm_cfg, pdm_in, pcm_out, 16u, 8u);
    TEST_ASSERT_TRUE(n_out >= 1u);

    mel[0] = 1.0f;
    mel[1] = 2.0f;
    mel[2] = 3.0f;
    mel[3] = 4.0f;
    bm_algo_mfcc_compute(&mfcc_cfg, mel, dct, mfcc);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, mfcc[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, mfcc[1]);

    bm_algo_image_rgb565_to_gray_u8(rgb565, gray, 2u, 2u);
    TEST_ASSERT_TRUE(gray[0] > 50u);
    TEST_ASSERT_TRUE(gray[1] > 100u);

    for (i = 0u; i < 16u; ++i) {
        src8[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL(0, bm_algo_image_crop_u8(src8, 4u, 4u, &rect, crop));
    TEST_ASSERT_EQUAL_UINT8(5u, crop[0]);

    bm_algo_moving_avg_q15_reset(&ma_st);
    TEST_ASSERT_TRUE(bm_algo_moving_avg_q15_step(&ma_st, &ma_cfg,
        bm_algo_float_to_q15(1.0f)) > 0);

    bm_algo_hysteresis_q31_reset(&hys_st);
    TEST_ASSERT_EQUAL(0, bm_algo_hysteresis_q31_step(&hys_st, &hys_cfg,
        bm_algo_float_to_q31(0.3f)));
    TEST_ASSERT_TRUE(bm_algo_hysteresis_q31_step(&hys_st, &hys_cfg,
        bm_algo_float_to_q31(0.6f)) > 0);
}

static void test_batch5_k0_and_fixed_batch4(void) {
    float ch0[16];
    float ch1[16];
    float out[16];
    const float *channels[2];
    int32_t delays[2] = { 0, 2 };
    uint8_t src[16];
    uint8_t dst[4];
    bm_algo_trapezoid_q31_config_t trap_cfg = {
        .max_vel_q31 = bm_algo_float_to_q31(1.0f),
        .max_accel_q31 = bm_algo_float_to_q31(10.0f),
        .max_decel_q31 = bm_algo_float_to_q31(10.0f)
    };
    bm_algo_trapezoid_q31_state_t trap_st;
    uint32_t i;

    for (i = 0u; i < 16u; ++i) {
        ch0[i] = 1.0f;
        ch1[i] = 0.0f;
        src[i] = (uint8_t)(i * 16u);
    }
    channels[0] = ch0;
    channels[1] = ch1;
    ch1[0] = 0.5f;
    bm_algo_delay_and_sum(channels, delays, 2u, 16u, out);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.75f, out[2]);

    TEST_ASSERT_EQUAL(0, bm_algo_image_resize_u8(src, 4u, 4u, dst, 2u, 2u));
    TEST_ASSERT_EQUAL_UINT8(0u, dst[0]);
    TEST_ASSERT_EQUAL_UINT8(160u, dst[3]);

    bm_algo_trapezoid_q31_reset(&trap_st, 0, 0);
    bm_algo_trapezoid_q31_set_target(&trap_st, bm_algo_float_to_q31(1.0f));
    TEST_ASSERT_TRUE(bm_algo_trapezoid_q31_step(&trap_st, &trap_cfg,
        bm_algo_float_to_q31(0.1f)) > 0);
}

static void test_ref_lpf1_q31_golden(void) {
    bm_algo_lpf1_q31_config_t cfg = {
        .alpha_q31 = REF_LPF1_Q31_ALPHA
    };
    bm_algo_lpf1_q31_state_t st;
    bm_algo_q31_t out;
    uint32_t i;
    uint32_t g;

    bm_algo_lpf1_q31_reset(&st, 0);
    for (i = 0u; i < REF_LPF1_Q31_WARMUP_STEPS; ++i) {
        (void)bm_algo_lpf1_q31_step(&st, &cfg, 0);
    }
    for (g = 0u; g < REF_LPF1_Q31_GOLDEN_COUNT; ++g) {
        out = bm_algo_lpf1_q31_step(&st, &cfg, REF_LPF1_Q31_STEP_INPUT);
        if (out > ref_lpf1_q31_golden[g]) {
            TEST_ASSERT_TRUE((out - ref_lpf1_q31_golden[g]) <=
                             REF_LPF1_Q31_TOLERANCE);
        } else {
            TEST_ASSERT_TRUE((ref_lpf1_q31_golden[g] - out) <=
                             REF_LPF1_Q31_TOLERANCE);
        }
    }
}

static void test_fixed_lpf1_q31(void) {
    bm_algo_lpf1_q31_config_t cfg = {
        .alpha_q31 = bm_algo_float_to_q31(0.1f)
    };
    bm_algo_lpf1_q31_state_t st;
    bm_algo_q31_t v = 0;
    int i;

    bm_algo_lpf1_q31_reset(&st, 0);
    for (i = 0; i < 500; ++i) {
        v = bm_algo_lpf1_q31_step(&st, &cfg, BM_ALGO_Q31_ONE);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, bm_algo_q31_to_float(v));
}

static void test_fixed_median3_q15(void) {
    bm_algo_median3_q15_state_t st;
    bm_algo_q15_t out;

    bm_algo_median3_q15_reset(&st);
    (void)bm_algo_median3_q15_step(&st, (bm_algo_q15_t)1000);
    (void)bm_algo_median3_q15_step(&st, (bm_algo_q15_t)3000);
    out = bm_algo_median3_q15_step(&st, (bm_algo_q15_t)2000);
    TEST_ASSERT_EQUAL_INT16(2000, out);

    out = bm_algo_median3_q15_step(&st, (bm_algo_q15_t)5000);
    TEST_ASSERT_EQUAL_INT16(3000, out);
}

static void test_fixed_hpf1_q15(void) {
    bm_algo_hpf1_q15_config_t cfg = {
        .alpha_q15 = bm_algo_float_to_q15(0.9f)
    };
    bm_algo_hpf1_q15_state_t st;
    bm_algo_q15_t first;
    bm_algo_q15_t settled = 0;
    int i;

    bm_algo_hpf1_q15_reset(&st);
    first = bm_algo_hpf1_q15_step(&st, &cfg, BM_ALGO_Q15_ONE);
    for (i = 0; i < 200; ++i) {
        settled = bm_algo_hpf1_q15_step(&st, &cfg, BM_ALGO_Q15_ONE);
    }

    TEST_ASSERT_TRUE(bm_algo_q15_to_float(first) > 0.5f);
    TEST_ASSERT_TRUE(fabsf(bm_algo_q15_to_float(settled)) < 0.05f);
}

static void test_batch6_fixed_batch5(void) {
    test_ref_lpf1_q31_golden();
    test_fixed_lpf1_q31();
    test_fixed_median3_q15();
    test_fixed_hpf1_q15();
}

static void test_mvdr_2ch_reference(void) {
    float ch0[REF_MVDR_2CH_BLOCK_SAMPLES];
    float ch1[REF_MVDR_2CH_BLOCK_SAMPLES];
    float out[REF_MVDR_2CH_BLOCK_SAMPLES];
    const float *channels[2];
    int32_t delays[2] = { 0, REF_MVDR_2CH_DELAY_CH1 };
    bm_algo_mvdr_config_t mvdr_cfg = {
        .diagonal_load = REF_MVDR_2CH_DIAGONAL_LOAD,
        .sample_hz = REF_MVDR_2CH_SAMPLE_HZ
    };
    uint32_t i;
    uint32_t peak_idx = 0u;
    float peak_val = 0.0f;
    float energy = 0.0f;

    for (i = 0u; i < REF_MVDR_2CH_BLOCK_SAMPLES; ++i) {
        ch0[i] = sinf(2.0f * 3.14159265f * 1000.0f * (float)i /
                      REF_MVDR_2CH_SAMPLE_HZ);
        ch1[i] = (i >= (uint32_t)REF_MVDR_2CH_DELAY_CH1)
                     ? sinf(2.0f * 3.14159265f * 1000.0f *
                            (float)(i - (uint32_t)REF_MVDR_2CH_DELAY_CH1) /
                            REF_MVDR_2CH_SAMPLE_HZ)
                     : 0.0f;
    }
    channels[0] = ch0;
    channels[1] = ch1;
    bm_algo_mvdr_beamform(channels, delays, 2u, REF_MVDR_2CH_BLOCK_SAMPLES,
                          &mvdr_cfg, out);

    for (i = 0u; i < REF_MVDR_2CH_BLOCK_SAMPLES; ++i) {
        float a = fabsf(out[i]);
        energy += out[i] * out[i];
        if (a > peak_val) {
            peak_val = a;
            peak_idx = i;
        }
    }
    energy /= (float)REF_MVDR_2CH_BLOCK_SAMPLES;
    TEST_ASSERT_TRUE(energy > REF_MVDR_2CH_ENERGY_MIN);
    TEST_ASSERT_TRUE(peak_idx >= REF_MVDR_2CH_PEAK_INDEX_MIN);
}

static void test_fixed_differentiator_q31(void) {
    bm_algo_differentiator_q31_config_t cfg = {
        .coeff_q31 = bm_algo_float_to_q31(0.5f)
    };
    bm_algo_differentiator_q31_state_t st;
    bm_algo_q31_t dt = bm_algo_float_to_q31(0.001f);
    bm_algo_q31_t d;

    bm_algo_differentiator_q31_reset(&st);
    (void)bm_algo_differentiator_q31_step(&st, &cfg, 0, dt);
    d = bm_algo_differentiator_q31_step(&st, &cfg, BM_ALGO_Q31_ONE, dt);
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(d) > 0.1f);
}

static void test_fixed_envelope_q15(void) {
    bm_algo_envelope_q15_config_t cfg = {
        .alpha_q15 = bm_algo_float_to_q15(0.2f)
    };
    bm_algo_envelope_q15_state_t st;
    bm_algo_q15_t env;
    int i;

    bm_algo_envelope_q15_reset(&st, 0);
    for (i = 0; i < 100; ++i) {
        env = bm_algo_envelope_q15_step(&st, &cfg, BM_ALGO_Q15_ONE);
    }
    TEST_ASSERT_TRUE(bm_algo_q15_to_float(env) > 0.5f);
}

static void test_ref_rms_q15_golden(void) {
    bm_algo_rms_q15_config_t cfg = {
        .window_size = REF_RMS_Q15_WINDOW_SIZE
    };
    bm_algo_rms_q15_state_t st;
    bm_algo_q15_t out;
    uint32_t i;

    bm_algo_rms_q15_reset(&st);
    for (i = 0u; i < REF_RMS_Q15_WARMUP_STEPS; ++i) {
        out = bm_algo_rms_q15_step(&st, &cfg, REF_RMS_Q15_STEP_INPUT);
    }
    if (out > REF_RMS_Q15_EXPECTED) {
        TEST_ASSERT_TRUE((out - REF_RMS_Q15_EXPECTED) <= REF_RMS_Q15_TOLERANCE);
    } else {
        TEST_ASSERT_TRUE((REF_RMS_Q15_EXPECTED - out) <= REF_RMS_Q15_TOLERANCE);
    }
}

static void test_batch7_k0_and_fixed_batch6(void) {
    test_mvdr_2ch_reference();
    test_fixed_differentiator_q31();
    test_fixed_envelope_q15();
    test_ref_rms_q15_golden();
}

static bm_algo_q31_t q31_abs_diff(bm_algo_q31_t a, bm_algo_q31_t b) {
    if (a > b) {
        return (bm_algo_q31_t)(a - b);
    }
    return (bm_algo_q31_t)(b - a);
}

static void test_ref_trapezoid_q31_golden(void) {
    bm_algo_trapezoid_q31_config_t cfg = {
        .max_vel_q31 = REF_TRAPEZOID_Q31_MAX_VEL,
        .max_accel_q31 = REF_TRAPEZOID_Q31_MAX_ACCEL,
        .max_decel_q31 = REF_TRAPEZOID_Q31_MAX_DECEL
    };
    bm_algo_trapezoid_q31_state_t st;
    bm_algo_q31_t pos;
    uint32_t g;

    bm_algo_trapezoid_q31_reset(&st, 0, 0);
    bm_algo_trapezoid_q31_set_target(&st, REF_TRAPEZOID_Q31_TARGET);
    for (g = 0u; g < REF_TRAPEZOID_Q31_GOLDEN_COUNT; ++g) {
        pos = bm_algo_trapezoid_q31_step(&st, &cfg, REF_TRAPEZOID_Q31_DT);
        TEST_ASSERT_TRUE(q31_abs_diff(pos, ref_trapezoid_q31_golden[g]) <=
                         REF_TRAPEZOID_Q31_TOLERANCE);
    }
}

static void test_ref_coulomb_q31_golden(void) {
    bm_algo_coulomb_q31_config_t cfg = {
        .nominal_capacity_q31 = REF_COULOMB_Q31_CAPACITY,
        .coulomb_efficiency_q31 = REF_COULOMB_Q31_EFFICIENCY,
        .soc_min = 0,
        .soc_max = BM_ALGO_Q31_ONE
    };
    bm_algo_coulomb_q31_state_t st;
    bm_algo_q31_t soc;
    uint32_t i;

    bm_algo_coulomb_q31_reset(&st, REF_COULOMB_Q31_SOC_INIT);
    for (i = 0u; i < REF_COULOMB_Q31_STEP_COUNT; ++i) {
        soc = bm_algo_coulomb_q31_step(&st, &cfg, REF_COULOMB_Q31_CURRENT,
                                       REF_COULOMB_Q31_DT);
        TEST_ASSERT_TRUE(q31_abs_diff(soc, ref_coulomb_q31_golden[i]) <=
                         REF_COULOMB_Q31_TOLERANCE);
    }
}

static void test_fixed_batch7_smoke(void) {
    bm_algo_hpf1_q31_config_t hpf_cfg = {
        .alpha_q31 = bm_algo_float_to_q31(0.9f)
    };
    bm_algo_hpf1_q31_state_t hpf_st;
    bm_algo_moving_avg_q31_config_t ma_cfg = { .window_size = 4u };
    bm_algo_moving_avg_q31_state_t ma_st;
    bm_algo_hysteresis_q15_config_t hys_cfg = {
        .low_threshold = bm_algo_float_to_q15(0.2f),
        .high_threshold = bm_algo_float_to_q15(0.5f)
    };
    bm_algo_hysteresis_q15_state_t hys_st;
    bm_algo_q31_t hpf_first;
    bm_algo_q31_t hpf_settled = 0;
    int i;

    TEST_ASSERT_EQUAL(0, bm_algo_deadband_q15(bm_algo_float_to_q15(0.01f),
        bm_algo_float_to_q15(0.1f)));
    TEST_ASSERT_TRUE(bm_algo_deadband_q15(bm_algo_float_to_q15(0.2f),
        bm_algo_float_to_q15(0.1f)) > 0);

    bm_algo_hpf1_q31_reset(&hpf_st);
    hpf_first = bm_algo_hpf1_q31_step(&hpf_st, &hpf_cfg, BM_ALGO_Q31_ONE);
    for (i = 0; i < 200; ++i) {
        hpf_settled = bm_algo_hpf1_q31_step(&hpf_st, &hpf_cfg, BM_ALGO_Q31_ONE);
    }
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(hpf_first) > 0.5f);
    TEST_ASSERT_TRUE(fabsf(bm_algo_q31_to_float(hpf_settled)) < 0.05f);

    bm_algo_moving_avg_q31_reset(&ma_st);
    TEST_ASSERT_TRUE(bm_algo_moving_avg_q31_step(&ma_st, &ma_cfg,
        BM_ALGO_Q31_ONE) > 0);

    bm_algo_hysteresis_q15_reset(&hys_st);
    TEST_ASSERT_EQUAL(0, bm_algo_hysteresis_q15_step(&hys_st, &hys_cfg,
        bm_algo_float_to_q15(0.3f)));
    TEST_ASSERT_TRUE(bm_algo_hysteresis_q15_step(&hys_st, &hys_cfg,
        bm_algo_float_to_q15(0.6f)) > 0);
}

static void test_batch8_fixed_batch7_and_refs(void) {
    test_ref_trapezoid_q31_golden();
    test_ref_coulomb_q31_golden();
    test_fixed_batch7_smoke();
}

static bm_algo_q15_t q15_abs_diff(bm_algo_q15_t a, bm_algo_q15_t b) {
    int32_t d = (int32_t)a - (int32_t)b;

    return (bm_algo_q15_t)((d < 0) ? -d : d);
}

static void test_ref_integrator_q15_golden(void) {
    bm_algo_integrator_q15_config_t cfg = {
        .min = REF_INTEGRATOR_Q15_MIN,
        .max = REF_INTEGRATOR_Q15_MAX
    };
    bm_algo_integrator_q15_state_t st;
    uint32_t g;

    bm_algo_integrator_q15_reset(&st, 0);
    for (g = 0u; g < REF_INTEGRATOR_Q15_GOLDEN_COUNT; ++g) {
        bm_algo_q15_t out = bm_algo_integrator_q15_step(&st, &cfg,
            REF_INTEGRATOR_Q15_INPUT, REF_INTEGRATOR_Q15_DT);
        TEST_ASSERT_TRUE(q15_abs_diff(out, ref_integrator_q15_golden[g]) <=
                         REF_INTEGRATOR_Q15_TOLERANCE);
    }
}

static void test_ref_lead_lag_q15_golden(void) {
    bm_algo_lead_lag_q15_config_t cfg = {
        .b0 = REF_LEAD_LAG_Q15_B0,
        .b1 = REF_LEAD_LAG_Q15_B1,
        .a1 = REF_LEAD_LAG_Q15_A1
    };
    bm_algo_lead_lag_q15_state_t st;
    uint32_t g;

    TEST_ASSERT_EQUAL(0, bm_algo_lead_lag_q15_init(&st, &cfg));
    for (g = 0u; g < REF_LEAD_LAG_Q15_GOLDEN_COUNT; ++g) {
        bm_algo_q15_t out = bm_algo_lead_lag_q15_step(&st, &cfg,
            ref_lead_lag_q15_inputs[g]);
        TEST_ASSERT_TRUE(q15_abs_diff(out, ref_lead_lag_q15_golden[g]) <=
                         REF_LEAD_LAG_Q15_TOLERANCE);
    }
}

static void test_fixed_batch8_smoke(void) {
    bm_algo_integrator_q15_config_t int_cfg = {
        .min = REF_INTEGRATOR_Q15_MIN,
        .max = REF_INTEGRATOR_Q15_MAX
    };
    bm_algo_integrator_q15_state_t int_st;
    bm_algo_rate_limit_q15_config_t rl_cfg = {
        .max_rise_per_s_q15 = bm_algo_float_to_q15(1.0f),
        .max_fall_per_s_q15 = bm_algo_float_to_q15(1.0f)
    };
    bm_algo_rate_limit_q15_state_t rl_st;
    bm_algo_biquad_q31_config_t bq_cfg = {
        .b0 = BM_ALGO_Q31_ONE,
        .b1 = 0,
        .b2 = 0,
        .a1 = 0,
        .a2 = 0
    };
    bm_algo_biquad_q31_state_t bq_st;
    bm_algo_q15_t int_final;
    uint32_t i;

    bm_algo_integrator_q15_reset(&int_st, 0);
    int_final = 0;
    for (i = 0u; i < REF_INTEGRATOR_Q15_WARMUP; ++i) {
        int_final = bm_algo_integrator_q15_step(&int_st, &int_cfg,
            REF_INTEGRATOR_Q15_INPUT, REF_INTEGRATOR_Q15_DT);
    }
    TEST_ASSERT_TRUE(q15_abs_diff(int_final, REF_INTEGRATOR_Q15_FINAL) <=
                     REF_INTEGRATOR_Q15_TOLERANCE);

    bm_algo_rate_limit_q15_reset(&rl_st, 0);
    TEST_ASSERT_TRUE(bm_algo_rate_limit_q15_step(&rl_st, &rl_cfg,
        bm_algo_float_to_q15(1.0f), REF_INTEGRATOR_Q15_DT) > 0);

    bm_algo_biquad_q31_reset(&bq_st);
    TEST_ASSERT_TRUE(bm_algo_biquad_q31_step(&bq_st, &bq_cfg,
        BM_ALGO_Q31_ONE) > 0);
}

static void test_batch9_fixed_batch8_and_refs(void) {
    test_ref_integrator_q15_golden();
    test_ref_lead_lag_q15_golden();
    test_fixed_batch8_smoke();
}

static void test_ref_dob_q15_golden(void) {
    bm_algo_dob_q15_config_t cfg = {
        .plant_gain_q15 = REF_DOB_Q15_PLANT_GAIN,
        .lpf_alpha_q15 = REF_DOB_Q15_LPF_ALPHA
    };
    bm_algo_dob_q15_state_t st;
    bm_algo_q15_t dist;
    bm_algo_q15_t out;

    bm_algo_dob_q15_reset(&st);
    out = bm_algo_dob_q15_step(&st, &cfg, REF_DOB_Q15_U, REF_DOB_Q15_Y, &dist);
    TEST_ASSERT_TRUE(q15_abs_diff(out, REF_DOB_Q15_EXPECTED) <=
                     REF_DOB_Q15_TOLERANCE);
    TEST_ASSERT_TRUE(q15_abs_diff(dist, REF_DOB_Q15_EXPECTED) <=
                     REF_DOB_Q15_TOLERANCE);
}

static void test_ref_biquad_q31_golden(void) {
    bm_algo_biquad_q31_config_t cfg = {
        .b0 = BM_ALGO_Q31_ONE,
        .b1 = 0,
        .b2 = 0,
        .a1 = 0,
        .a2 = 0
    };
    bm_algo_biquad_q31_state_t st;
    uint32_t g;
    bm_algo_q31_t out;

    bm_algo_biquad_q31_reset(&st);
    for (g = 0u; g < REF_BIQUAD_Q31_GOLDEN_COUNT; ++g) {
        out = bm_algo_biquad_q31_step(&st, &cfg, REF_BIQUAD_Q31_STEP_INPUT);
        TEST_ASSERT_TRUE(q31_abs_diff(out, ref_biquad_q31_golden[g]) <=
                         REF_BIQUAD_Q31_TOLERANCE);
    }
}

static void test_fixed_batch9_smoke(void) {
    bm_algo_envelope_q31_config_t env_cfg = {
        .alpha_q31 = bm_algo_float_to_q31(0.1f)
    };
    bm_algo_envelope_q31_state_t env_st;
    bm_algo_rms_q31_config_t rms_cfg = {
        .window_size = 4u
    };
    bm_algo_rms_q31_state_t rms_st;
    bm_algo_backlash_q31_state_t bl_st;
    bm_algo_q31_t env_out;
    bm_algo_q31_t rms_out;
    bm_algo_q31_t bl_out;
    uint32_t i;

    bm_algo_envelope_q31_reset(&env_st, 0);
    for (i = 0u; i < 8u; ++i) {
        env_out = bm_algo_envelope_q31_step(&env_st, &env_cfg, BM_ALGO_Q31_ONE);
    }
    TEST_ASSERT_TRUE(env_out > 0);

    bm_algo_rms_q31_reset(&rms_st);
    for (i = 0u; i < 4u; ++i) {
        rms_out = bm_algo_rms_q31_step(&rms_st, &rms_cfg, BM_ALGO_Q31_ONE);
    }
    TEST_ASSERT_TRUE(rms_out > 0);

    bm_algo_backlash_q31_reset(&bl_st);
    (void)bm_algo_backlash_inverse_q31(bm_algo_float_to_q31(0.5f), &bl_st,
                                       bm_algo_float_to_q31(0.2f),
                                       bm_algo_float_to_q31(0.05f));
    bl_out = bm_algo_backlash_inverse_q31(bm_algo_float_to_q31(-0.5f), &bl_st,
                                          bm_algo_float_to_q31(0.2f),
                                          bm_algo_float_to_q31(0.05f));
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(bl_out) <
                     bm_algo_q31_to_float(bm_algo_float_to_q31(0.5f)));
}

static void test_batch10_fixed_batch9_and_refs(void) {
    test_ref_dob_q15_golden();
    test_ref_biquad_q31_golden();
    test_fixed_batch9_smoke();
}

static void test_ref_differentiator_q15_golden(void) {
    bm_algo_differentiator_q15_config_t cfg = {
        .coeff_q15 = REF_DIFFERENTIATOR_Q15_COEFF
    };
    bm_algo_differentiator_q15_state_t st;
    uint32_t g;
    bm_algo_q15_t out;

    bm_algo_differentiator_q15_reset(&st);
    for (g = 0u; g < REF_DIFFERENTIATOR_Q15_STEP_COUNT; ++g) {
        out = bm_algo_differentiator_q15_step(&st, &cfg,
            ref_differentiator_q15_inputs[g], REF_DIFFERENTIATOR_Q15_DT);
        TEST_ASSERT_TRUE(q15_abs_diff(out, ref_differentiator_q15_golden[g]) <=
                         REF_DIFFERENTIATOR_Q15_TOLERANCE);
    }
}

static void test_ref_coulomb_q15_golden(void) {
    bm_algo_coulomb_q15_config_t cfg = {
        .nominal_capacity_q15 = REF_COULOMB_Q15_CAPACITY,
        .coulomb_efficiency_q15 = REF_COULOMB_Q15_EFFICIENCY,
        .soc_min = 0,
        .soc_max = BM_ALGO_Q15_ONE
    };
    bm_algo_coulomb_q15_state_t st;
    bm_algo_q15_t soc;
    uint32_t i;

    bm_algo_coulomb_q15_reset(&st, REF_COULOMB_Q15_SOC_INIT);
    for (i = 0u; i < REF_COULOMB_Q15_STEP_COUNT; ++i) {
        soc = bm_algo_coulomb_q15_step(&st, &cfg, REF_COULOMB_Q15_CURRENT,
                                       REF_COULOMB_Q15_DT);
        TEST_ASSERT_TRUE(q15_abs_diff(soc, ref_coulomb_q15_golden[i]) <=
                         REF_COULOMB_Q15_TOLERANCE);
    }
}

static void test_fixed_batch10_smoke(void) {
    bm_algo_dob_q31_config_t dob_cfg = {
        .plant_gain_q31 = bm_algo_float_to_q31(0.5f),
        .lpf_alpha_q31 = bm_algo_float_to_q31(0.5f)
    };
    bm_algo_dob_q31_state_t dob_st;
    bm_algo_lead_lag_q31_config_t ll_cfg = {
        .b0 = BM_ALGO_Q31_ONE,
        .b1 = 0,
        .a1 = 0
    };
    bm_algo_lead_lag_q31_state_t ll_st;
    bm_algo_complementary_q15_config_t comp_cfg = {
        .alpha_q15 = bm_algo_float_to_q15(0.98f)
    };
    bm_algo_complementary_q15_state_t comp_st;
    bm_algo_q31_t dob_out;
    bm_algo_q31_t ll_out;
    bm_algo_q31_t ff_out;

    bm_algo_dob_q31_reset(&dob_st);
    dob_out = bm_algo_dob_q31_step(&dob_st, &dob_cfg,
        bm_algo_float_to_q31(1.0f), bm_algo_float_to_q31(0.8f), NULL);
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(dob_out) > 0.0f);

    TEST_ASSERT_EQUAL(0, bm_algo_lead_lag_q31_init(&ll_st, &ll_cfg));
    ll_out = bm_algo_lead_lag_q31_step(&ll_st, &ll_cfg, BM_ALGO_Q31_ONE);
    TEST_ASSERT_TRUE(ll_out > 0);

    ff_out = bm_algo_feedforward_q31_step(BM_ALGO_Q31_ONE,
        bm_algo_float_to_q31(0.5f), bm_algo_float_to_q31(0.1f));
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(ff_out) > 0.5f);

    TEST_ASSERT_TRUE(bm_algo_feedforward_q15_step(BM_ALGO_Q15_ONE,
        bm_algo_float_to_q15(0.5f), bm_algo_float_to_q15(0.1f)) > 0);

    bm_algo_complementary_q15_reset(&comp_st);
    bm_algo_complementary_q15_step(&comp_st, &comp_cfg,
        bm_algo_float_to_q15(0.1f), 0, 0,
        0, 0, BM_ALGO_Q15_ONE,
        bm_algo_float_to_q15(0.01f));
    TEST_ASSERT_TRUE(comp_st.roll_rad != 0 || comp_st.pitch_rad != 0);
}

static void test_batch11_fixed_batch10_and_refs(void) {
    test_ref_differentiator_q15_golden();
    test_ref_coulomb_q15_golden();
    test_fixed_batch10_smoke();
}

static void test_ref_pi_q15_golden(void) {
    bm_algo_pi_q15_config_t cfg = {
        .kp = REF_PI_Q15_KP,
        .ki = REF_PI_Q15_KI,
        .out_min = REF_PI_Q15_OUT_MIN,
        .out_max = REF_PI_Q15_OUT_MAX,
        .integrator_min = REF_PI_Q15_OUT_MIN,
        .integrator_max = REF_PI_Q15_OUT_MAX
    };
    bm_algo_pi_q15_state_t st;
    bm_algo_q15_t out;
    uint32_t g;

    bm_algo_pi_q15_reset(&st, 0);
    for (g = 0u; g < REF_PI_Q15_GOLDEN_COUNT; ++g) {
        out = bm_algo_pi_q15_step(&st, &cfg, REF_PI_Q15_ERROR, REF_PI_Q15_DT);
        TEST_ASSERT_TRUE(q15_abs_diff(out, ref_pi_q15_golden[g]) <=
                         REF_PI_Q15_TOLERANCE);
    }
}

static void test_ref_trapezoid_q15_golden(void) {
    bm_algo_trapezoid_q15_config_t cfg = {
        .max_vel_q15 = REF_TRAPEZOID_Q15_MAX_VEL,
        .max_accel_q15 = REF_TRAPEZOID_Q15_MAX_ACCEL,
        .max_decel_q15 = REF_TRAPEZOID_Q15_MAX_DECEL
    };
    bm_algo_trapezoid_q15_state_t st;
    bm_algo_q15_t pos;
    uint32_t g;

    bm_algo_trapezoid_q15_reset(&st, 0, 0);
    bm_algo_trapezoid_q15_set_target(&st, REF_TRAPEZOID_Q15_TARGET);
    for (g = 0u; g < REF_TRAPEZOID_Q15_GOLDEN_COUNT; ++g) {
        pos = bm_algo_trapezoid_q15_step(&st, &cfg, REF_TRAPEZOID_Q15_DT);
        TEST_ASSERT_TRUE(q15_abs_diff(pos, ref_trapezoid_q15_golden[g]) <=
                         REF_TRAPEZOID_Q15_TOLERANCE);
    }
}

static void test_fixed_batch11_smoke(void) {
    bm_algo_pr_q31_config_t pr_cfg = {
        .b0 = BM_ALGO_Q31_ONE,
        .b1 = 0,
        .b2 = 0,
        .a1 = 0,
        .a2 = 0,
        .out_min = (bm_algo_q31_t)INT32_MIN,
        .out_max = BM_ALGO_Q31_ONE
    };
    bm_algo_pr_q31_state_t pr_st;
    bm_algo_ramp_q15_config_t ramp_cfg = {
        .rate_per_s_q15 = bm_algo_float_to_q15(1.0f)
    };
    bm_algo_ramp_q15_state_t ramp_st;
    bm_algo_redundant_pair_q15_config_t rp15_cfg = {
        .tolerance_abs = bm_algo_float_to_q15(0.01f),
        .tolerance_rel = bm_algo_float_to_q15(0.05f)
    };
    bm_algo_redundant_pair_q31_config_t rp31_cfg = {
        .tolerance_abs = bm_algo_float_to_q31(0.01f),
        .tolerance_rel = bm_algo_float_to_q31(0.05f)
    };
    bm_algo_rate_est_q15_state_t rate_st;
    bm_algo_soc_fusion_q15_config_t fusion_cfg = {
        .ocv_weight = bm_algo_float_to_q15(0.3f)
    };
    bm_algo_q31_t pr_out;
    bm_algo_q15_t ramp_out;
    bm_algo_q15_t rate_out;
    bm_algo_q15_t fused;

    bm_algo_pr_q31_reset(&pr_st);
    pr_out = bm_algo_pr_q31_step(&pr_st, &pr_cfg, BM_ALGO_Q31_ONE);
    TEST_ASSERT_TRUE(pr_out > 0);

    bm_algo_ramp_q15_reset(&ramp_st, 0);
    ramp_out = bm_algo_ramp_q15_step(&ramp_st, &ramp_cfg,
                                       BM_ALGO_Q15_ONE,
                                       bm_algo_float_to_q15(0.1f));
    TEST_ASSERT_TRUE(ramp_out > 0);

    TEST_ASSERT_EQUAL(0u, bm_algo_redundant_pair_q15_step(
        BM_ALGO_Q15_ONE, BM_ALGO_Q15_ONE, &rp15_cfg));
    TEST_ASSERT_NOT_EQUAL(0u, bm_algo_redundant_pair_q15_step(
        BM_ALGO_Q15_ONE, 0, &rp15_cfg));

    TEST_ASSERT_EQUAL(0u, bm_algo_redundant_pair_q31_step(
        BM_ALGO_Q31_ONE, BM_ALGO_Q31_ONE, &rp31_cfg));
    TEST_ASSERT_NOT_EQUAL(0u, bm_algo_redundant_pair_q31_step(
        BM_ALGO_Q31_ONE, 0, &rp31_cfg));

    bm_algo_rate_est_q15_reset(&rate_st, 0);
    rate_out = bm_algo_rate_est_q15_step(&rate_st, BM_ALGO_Q15_ONE,
                                         bm_algo_float_to_q15(0.1f));
    TEST_ASSERT_TRUE(rate_out > 0);

    fused = bm_algo_soc_fusion_q15_step(bm_algo_float_to_q15(0.5f),
                                        bm_algo_float_to_q15(0.8f),
                                        &fusion_cfg);
    TEST_ASSERT_TRUE(bm_algo_q15_to_float(fused) > 0.5f);
    TEST_ASSERT_TRUE(bm_algo_q15_to_float(fused) < 0.8f);
}

static void test_batch12_fixed_batch11_and_refs(void) {
    test_ref_pi_q15_golden();
    test_ref_trapezoid_q15_golden();
    test_fixed_batch11_smoke();
}

static void test_ref_mppt_po_q15_golden(void) {
    bm_algo_mppt_po_q15_config_t cfg = {
        .step_v_q15 = REF_MPPT_PO_Q15_STEP_V,
        .v_min_q15 = REF_MPPT_PO_Q15_V_MIN,
        .v_max_q15 = REF_MPPT_PO_Q15_V_MAX
    };
    bm_algo_mppt_po_q15_state_t st;
    bm_algo_q15_t v_ref;
    uint32_t g;

    bm_algo_mppt_po_q15_reset(&st, REF_MPPT_PO_Q15_V_INIT);
    for (g = 0u; g < REF_MPPT_PO_Q15_GOLDEN_COUNT; ++g) {
        v_ref = bm_algo_mppt_po_q15_step(&st, &cfg,
            ref_mppt_po_q15_voltage[g],
            ref_mppt_po_q15_current[g]);
        TEST_ASSERT_TRUE(q15_abs_diff(v_ref, ref_mppt_po_q15_golden[g]) <=
                         REF_MPPT_PO_Q15_TOLERANCE);
    }
}

static void test_ref_scurve_q15_golden(void) {
    bm_algo_scurve_q15_config_t cfg = {
        .max_vel_q15 = REF_SCURVE_Q15_MAX_VEL,
        .max_accel_q15 = REF_SCURVE_Q15_MAX_ACCEL,
        .max_jerk_q15 = REF_SCURVE_Q15_MAX_JERK
    };
    bm_algo_scurve_q15_state_t st;
    bm_algo_q15_t pos;
    uint32_t g;

    bm_algo_scurve_q15_reset(&st, 0, 0, 0);
    bm_algo_scurve_q15_set_target(&st, REF_SCURVE_Q15_TARGET);
    for (g = 0u; g < REF_SCURVE_Q15_GOLDEN_COUNT; ++g) {
        pos = bm_algo_scurve_q15_step(&st, &cfg, REF_SCURVE_Q15_DT);
        TEST_ASSERT_TRUE(q15_abs_diff(pos, ref_scurve_q15_golden[g]) <=
                         REF_SCURVE_Q15_TOLERANCE);
    }
}

static void test_fixed_batch12_smoke(void) {
    bm_algo_rate_est_q31_state_t rate31;
    bm_algo_soc_fusion_q31_config_t fusion31_cfg = {
        .ocv_weight = bm_algo_float_to_q31(0.25f)
    };
    bm_algo_mppt_ic_q15_config_t ic_cfg = {
        .step_v_q15 = bm_algo_float_to_q15(0.01f),
        .v_min_q15 = 0,
        .v_max_q15 = BM_ALGO_Q15_ONE
    };
    bm_algo_mppt_ic_q15_state_t ic_st;
    bm_algo_range_monitor_q15_config_t mon_cfg = {
        .min_v_q15 = bm_algo_float_to_q15(0.0f),
        .max_v_q15 = bm_algo_float_to_q15(0.5f),
        .max_rate_per_s_q15 = bm_algo_float_to_q15(10.0f)
    };
    bm_algo_range_monitor_q15_state_t mon_st;
    bm_algo_debounce_analog_q15_config_t deb_cfg = {
        .stable_count_required = 1u,
        .tolerance_q15 = bm_algo_float_to_q15(0.01f)
    };
    bm_algo_debounce_analog_q15_state_t deb_st;
    bm_algo_energy_wh_q15_state_t wh_st;
    bm_algo_q31_t rate31_out;
    bm_algo_q31_t fused31;
    bm_algo_q31_t wh_acc;
    uint32_t flags;
    int deb_ok;

    bm_algo_rate_est_q31_reset(&rate31, 0);
    rate31_out = bm_algo_rate_est_q31_step(&rate31, BM_ALGO_Q31_ONE,
                                           bm_algo_float_to_q31(0.1f));
    TEST_ASSERT_TRUE(rate31_out > 0);

    fused31 = bm_algo_soc_fusion_q31_step(bm_algo_float_to_q31(0.4f),
                                          bm_algo_float_to_q31(0.9f),
                                          &fusion31_cfg);
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(fused31) > 0.4f);
    TEST_ASSERT_TRUE(bm_algo_q31_to_float(fused31) < 0.9f);

    bm_algo_mppt_ic_q15_reset(&ic_st, bm_algo_float_to_q15(0.4f));
    (void)bm_algo_mppt_ic_q15_step(&ic_st, &ic_cfg,
        bm_algo_float_to_q15(0.41f), bm_algo_float_to_q15(0.5f));
    TEST_ASSERT_TRUE(ic_st.v_ref_q15 > 0);

    bm_algo_range_monitor_q15_reset(&mon_st, bm_algo_float_to_q15(0.5f));
    flags = bm_algo_range_monitor_q15_step(&mon_st, &mon_cfg,
        bm_algo_float_to_q15(0.6f),
        bm_algo_float_to_q15(0.01f));
    TEST_ASSERT_NOT_EQUAL(0u, flags & BM_ALGO_FAULT_OVER_RANGE);

    bm_algo_debounce_analog_q15_reset(&deb_st, bm_algo_float_to_q15(0.2f));
    deb_ok = bm_algo_debounce_analog_q15_step(&deb_st, &deb_cfg,
        bm_algo_float_to_q15(0.2f));
    TEST_ASSERT_EQUAL(1, deb_ok);

    bm_algo_energy_wh_q15_reset(&wh_st);
    wh_acc = bm_algo_energy_wh_integrator_q15_step(&wh_st, BM_ALGO_Q15_ONE,
        bm_algo_float_to_q15(0.1f));
    TEST_ASSERT_TRUE(wh_acc > 0);
}

static void test_batch13_fixed_batch12_and_refs(void) {
    test_ref_mppt_po_q15_golden();
    test_ref_scurve_q15_golden();
    test_fixed_batch12_smoke();
}

static bm_algo_q15_t q15_abs_diff_local(bm_algo_q15_t a, bm_algo_q15_t b) {
    return (a >= b) ? (bm_algo_q15_t)(a - b) : (bm_algo_q15_t)(b - a);
}

static bm_algo_q31_t q31_abs_diff_local(bm_algo_q31_t a, bm_algo_q31_t b) {
    return (a >= b) ? (bm_algo_q31_t)(a - b) : (bm_algo_q31_t)(b - a);
}

static void test_ref_median_q15_golden(void) {
    bm_algo_median_q15_config_t cfg = { .window_size = REF_MEDIAN_Q15_WINDOW };
    bm_algo_median_q15_state_t st;
    bm_algo_q15_t out;
    uint32_t g;

    bm_algo_median_q15_reset(&st);
    for (g = 0u; g < REF_MEDIAN_Q15_GOLDEN_COUNT; ++g) {
        out = bm_algo_median_q15_step(&st, &cfg, ref_median_q15_inputs[g]);
        TEST_ASSERT_TRUE(q15_abs_diff_local(out, ref_median_q15_golden[g]) <=
                         REF_MEDIAN_Q15_TOLERANCE);
    }
}

static void test_ref_pid2_q15_golden(void) {
    bm_algo_pid2_q15_config_t cfg = {
        .kp_q15 = BM_ALGO_Q15_ONE,
        .ki_q15 = 0,
        .kd_q15 = 0,
        .b_q15 = BM_ALGO_Q15_ONE,
        .out_min = (bm_algo_q15_t)-32768,
        .out_max = BM_ALGO_Q15_ONE,
        .integrator_min = (bm_algo_q15_t)-32768,
        .integrator_max = BM_ALGO_Q15_ONE,
        .d_filter_coeff_q15 = 0
    };
    bm_algo_pid2_q15_state_t st;
    bm_algo_q15_t out;

    bm_algo_pid2_q15_reset(&st, 0);
    out = bm_algo_pid2_q15_step(&st, &cfg, REF_PID2_Q15_REFERENCE,
                                REF_PID2_Q15_MEASUREMENT, REF_PID2_Q15_DT);
    TEST_ASSERT_TRUE(q15_abs_diff_local(out, REF_PID2_Q15_EXPECTED) <=
                     REF_PID2_Q15_TOLERANCE);
}

static void test_ref_mppt_po_q31_golden(void) {
    bm_algo_mppt_po_q31_config_t cfg = {
        .step_v_q31 = REF_MPPT_PO_Q31_STEP_V,
        .v_min_q31 = REF_MPPT_PO_Q31_V_MIN,
        .v_max_q31 = REF_MPPT_PO_Q31_V_MAX
    };
    bm_algo_mppt_po_q31_state_t st;
    bm_algo_q31_t v_ref;
    uint32_t g;

    bm_algo_mppt_po_q31_reset(&st, REF_MPPT_PO_Q31_V_INIT);
    for (g = 0u; g < REF_MPPT_PO_Q31_GOLDEN_COUNT; ++g) {
        v_ref = bm_algo_mppt_po_q31_step(&st, &cfg,
            ref_mppt_po_q31_voltage[g], ref_mppt_po_q31_current[g]);
        TEST_ASSERT_TRUE(q31_abs_diff_local(v_ref, ref_mppt_po_q31_golden[g]) <=
                         REF_MPPT_PO_Q31_TOLERANCE);
    }
}

static bm_algo_q15_t s_batch14_smith15_delay[2];
static bm_algo_q31_t s_batch14_smith31_delay[2];
static bm_algo_q15_t s_batch14_lrs15_out[4];
static bm_algo_q31_t s_batch14_lrs31_out[4];

static void test_fixed_batch14_smoke_core(void) {
    bm_algo_complementary_q31_config_t comp31_cfg = {
        .alpha_q31 = bm_algo_float_to_q31(0.98f)
    };
    bm_algo_complementary_q31_state_t comp31_st;
    bm_algo_dda_q15_config_t dda15_cfg = {
        .x0_q15 = 0,
        .y0_q15 = 0,
        .x1_q15 = BM_ALGO_Q15_ONE,
        .y1_q15 = BM_ALGO_Q15_ONE,
        .step_size_q15 = bm_algo_float_to_q15(0.1f)
    };
    bm_algo_dda_q15_state_t dda15_st;
    bm_algo_dda_q31_config_t dda31_cfg = {
        .x0_q31 = 0,
        .y0_q31 = 0,
        .x1_q31 = BM_ALGO_Q31_ONE,
        .y1_q31 = BM_ALGO_Q31_ONE,
        .step_size_q31 = bm_algo_float_to_q31(0.1f)
    };
    bm_algo_dda_q31_state_t dda31_st;
    bm_algo_debounce_analog_q31_config_t deb31_cfg = {
        .stable_count_required = 1u,
        .tolerance_q31 = bm_algo_float_to_q31(0.01f)
    };
    bm_algo_debounce_analog_q31_state_t deb31_st;
    bm_algo_decimator_q15_state_t dec15_st;
    bm_algo_decimator_q31_state_t dec31_st;
    bm_algo_encoder_diag_q15_config_t enc15_cfg = { .max_delta_per_step = 4 };
    bm_algo_encoder_diag_q15_state_t enc15_st;
    bm_algo_encoder_diag_q31_config_t enc31_cfg = { .max_delta_per_step = 4 };
    bm_algo_encoder_diag_q31_state_t enc31_st;
    bm_algo_energy_wh_q31_state_t wh31_st;
    bm_algo_q15_t fir15_coeffs[2] = { BM_ALGO_Q15_ONE, 0 };
    bm_algo_q15_t fir15_delay[2];
    bm_algo_fir_q15_config_t fir15_cfg = {
        .coeffs = fir15_coeffs,
        .tap_count = 2u,
        .delay_line = fir15_delay
    };
    bm_algo_fir_q15_state_t fir15_st;
    bm_algo_q31_t fir31_coeffs[2] = { BM_ALGO_Q31_ONE, 0 };
    bm_algo_q31_t fir31_delay[2];
    bm_algo_fir_q31_config_t fir31_cfg = {
        .coeffs = fir31_coeffs,
        .tap_count = 2u,
        .delay_line = fir31_delay
    };
    bm_algo_fir_q31_state_t fir31_st;
    bm_algo_q15_t dda_x15;
    bm_algo_q15_t dda_y15;
    bm_algo_q31_t dda_x31;
    bm_algo_q31_t dda_y31;
    bm_algo_q15_t dec15_out;
    bm_algo_q31_t dec31_out;
    bm_algo_q31_t wh31;
    bm_algo_q15_t fir15_out;
    bm_algo_q31_t fir31_out;
    uint32_t enc_faults;
    int dda_ok;
    int deb31_ok;
    int dec_ok;

    bm_algo_complementary_q31_reset(&comp31_st);
    bm_algo_complementary_q31_step(&comp31_st, &comp31_cfg,
        bm_algo_float_to_q31(0.1f), 0, 0,
        bm_algo_float_to_q31(0.0f), bm_algo_float_to_q31(0.0f),
        BM_ALGO_Q31_ONE, bm_algo_float_to_q31(0.01f));
    TEST_ASSERT_TRUE(comp31_st.roll_rad != 0);

    bm_algo_dda_q15_reset(&dda15_st, &dda15_cfg);
    dda_ok = bm_algo_dda_q15_step(&dda15_st, &dda15_cfg, &dda_x15, &dda_y15);
    TEST_ASSERT_EQUAL(1, dda_ok);

    bm_algo_dda_q31_reset(&dda31_st, &dda31_cfg);
    dda_ok = bm_algo_dda_q31_step(&dda31_st, &dda31_cfg, &dda_x31, &dda_y31);
    TEST_ASSERT_EQUAL(1, dda_ok);

    bm_algo_debounce_analog_q31_reset(&deb31_st, bm_algo_float_to_q31(0.2f));
    deb31_ok = bm_algo_debounce_analog_q31_step(&deb31_st, &deb31_cfg,
        bm_algo_float_to_q31(0.2f));
    TEST_ASSERT_EQUAL(1, deb31_ok);

    bm_algo_decimator_q15_reset(&dec15_st);
    dec_ok = bm_algo_decimator_q15_step(&dec15_st, 2u, BM_ALGO_Q15_ONE,
        &dec15_out);
    TEST_ASSERT_EQUAL(1, dec_ok);

    bm_algo_decimator_q31_reset(&dec31_st);
    dec_ok = bm_algo_decimator_q31_step(&dec31_st, 2u, BM_ALGO_Q31_ONE,
        &dec31_out);
    TEST_ASSERT_EQUAL(1, dec_ok);

    bm_algo_encoder_diag_q15_reset(&enc15_st, 0);
    enc_faults = bm_algo_encoder_diag_q15_step(&enc15_st, &enc15_cfg, 10, 0);
    TEST_ASSERT_NOT_EQUAL(0u, enc_faults & BM_ALGO_ENCODER_FAULT_MISSED);

    bm_algo_encoder_diag_q31_reset(&enc31_st, 0);
    enc_faults = bm_algo_encoder_diag_q31_step(&enc31_st, &enc31_cfg, 10, 1);
    TEST_ASSERT_NOT_EQUAL(0u, enc_faults & BM_ALGO_ENCODER_FAULT_INDEX);

    bm_algo_energy_wh_q31_reset(&wh31_st);
    wh31 = bm_algo_energy_wh_integrator_q31_step(&wh31_st, BM_ALGO_Q31_ONE,
        bm_algo_float_to_q31(0.1f));
    TEST_ASSERT_TRUE(wh31 > 0);

    TEST_ASSERT_EQUAL(0, bm_algo_fir_q15_init(&fir15_st, &fir15_cfg));
    fir15_out = bm_algo_fir_q15_step(&fir15_st, &fir15_cfg, BM_ALGO_Q15_ONE);
    TEST_ASSERT_TRUE(fir15_out >= (bm_algo_q15_t)32000);

    TEST_ASSERT_EQUAL(0, bm_algo_fir_q31_init(&fir31_st, &fir31_cfg));
    fir31_out = bm_algo_fir_q31_step(&fir31_st, &fir31_cfg, BM_ALGO_Q31_ONE);
    TEST_ASSERT_TRUE(fir31_out > 0);
}

static void test_fixed_batch14_smoke_fusion(void) {
    bm_algo_flux_observer_q15_config_t flux15_cfg = {
        .rs_q15 = bm_algo_float_to_q15(0.1f),
        .ls_q15 = bm_algo_float_to_q15(0.001f),
        .pll_kp_q15 = bm_algo_float_to_q15(10.0f),
        .pll_ki_q15 = bm_algo_float_to_q15(100.0f)
    };
    bm_algo_flux_observer_q15_state_t flux15_st;
    bm_algo_flux_observer_q31_config_t flux31_cfg = {
        .rs_q31 = bm_algo_float_to_q31(0.1f),
        .ls_q31 = bm_algo_float_to_q31(0.001f),
        .pll_kp_q31 = bm_algo_float_to_q31(10.0f),
        .pll_ki_q31 = bm_algo_float_to_q31(100.0f)
    };
    bm_algo_flux_observer_q31_state_t flux31_st;
    bm_algo_linear_resampler_q15_state_t lrs15_st;
    bm_algo_linear_resampler_q31_state_t lrs31_st;
    bm_algo_madgwick_q15_config_t madg15_cfg = {
        .beta_q15 = bm_algo_float_to_q15(0.1f)
    };
    bm_algo_madgwick_q15_state_t madg15_st;
    bm_algo_madgwick_q31_config_t madg31_cfg = {
        .beta_q31 = bm_algo_float_to_q31(0.1f)
    };
    bm_algo_madgwick_q31_state_t madg31_st;
    bm_algo_mahony_q15_config_t mah15_cfg = {
        .kp_q15 = bm_algo_float_to_q15(1.0f),
        .ki_q15 = bm_algo_float_to_q15(0.0f)
    };
    bm_algo_mahony_q15_state_t mah15_st;
    bm_algo_mahony_q31_config_t mah31_cfg = {
        .kp_q31 = bm_algo_float_to_q31(1.0f),
        .ki_q31 = bm_algo_float_to_q31(0.0f)
    };
    bm_algo_mahony_q31_state_t mah31_st;
    bm_algo_median_q15_config_t med15_cfg = { .window_size = 3u };
    bm_algo_median_q15_state_t med15_st;
    bm_algo_median_q31_config_t med31_cfg = { .window_size = 3u };
    bm_algo_median_q31_state_t med31_st;
    bm_algo_mppt_ic_q31_config_t ic31_cfg = {
        .step_v_q31 = bm_algo_float_to_q31(0.01f),
        .v_min_q31 = 0,
        .v_max_q31 = BM_ALGO_Q31_ONE
    };
    bm_algo_mppt_ic_q31_state_t ic31_st;
    bm_algo_mppt_po_q31_config_t po31_cfg = {
        .step_v_q31 = bm_algo_float_to_q31(0.01f),
        .v_min_q31 = 0,
        .v_max_q31 = BM_ALGO_Q31_ONE
    };
    bm_algo_mppt_po_q31_state_t po31_st;
    bm_algo_pid2_q31_config_t pid231_cfg = {
        .kp_q31 = BM_ALGO_Q31_ONE,
        .ki_q31 = 0,
        .kd_q31 = 0,
        .b_q31 = BM_ALGO_Q31_ONE,
        .out_min = (bm_algo_q31_t)INT32_MIN,
        .out_max = BM_ALGO_Q31_ONE,
        .integrator_min = (bm_algo_q31_t)INT32_MIN,
        .integrator_max = BM_ALGO_Q31_ONE,
        .d_filter_alpha_q31 = 0
    };
    bm_algo_pid2_q31_state_t pid231_st;
    bm_algo_range_monitor_q31_config_t mon31_cfg = {
        .min_v_q31 = 0,
        .max_v_q31 = bm_algo_float_to_q31(0.5f),
        .max_rate_per_s_q31 = bm_algo_float_to_q31(10.0f)
    };
    bm_algo_range_monitor_q31_state_t mon31_st;
    bm_algo_smith_predictor_q15_config_t smith15_cfg = {
        .model_gain_q15 = BM_ALGO_Q15_ONE,
        .delay_steps = 2u
    };
    bm_algo_smith_predictor_q15_state_t smith15_st;
    bm_algo_smith_predictor_q31_config_t smith31_cfg = {
        .model_gain_q31 = BM_ALGO_Q31_ONE,
        .delay_steps = 2u
    };
    bm_algo_smith_predictor_q31_state_t smith31_st;
    bm_algo_sogi_pll_q15_config_t sogi15_cfg = {
        .nominal_omega_q15 = bm_algo_float_to_q15(314.0f),
        .k_sogi_q15 = bm_algo_float_to_q15(1.0f),
        .k_pll_q15 = bm_algo_float_to_q15(10.0f)
    };
    bm_algo_sogi_pll_q15_state_t sogi15_st;
    bm_algo_sogi_pll_q31_config_t sogi31_cfg = {
        .nominal_omega_q31 = bm_algo_float_to_q31(314.0f),
        .k_sogi_q31 = bm_algo_float_to_q31(1.0f),
        .k_pll_q31 = bm_algo_float_to_q31(10.0f)
    };
    bm_algo_sogi_pll_q31_state_t sogi31_st;
    bm_algo_q31_t pid231_out;
    bm_algo_q15_t smith15_err;
    bm_algo_q31_t smith31_err;
    uint32_t mon_flags;
    uint32_t lrs_count;
    int lrs_ok;

    bm_algo_flux_observer_q15_reset(&flux15_st, 0);
    (void)bm_algo_flux_observer_q15_step(
        &flux15_st, &flux15_cfg, BM_ALGO_Q15_ONE, 0, 0, 0,
        bm_algo_float_to_q15(0.001f));

    bm_algo_flux_observer_q31_reset(&flux31_st, 0);
    (void)bm_algo_flux_observer_q31_step(
        &flux31_st, &flux31_cfg, BM_ALGO_Q31_ONE, 0, 0, 0,
        bm_algo_float_to_q31(0.001f));

    bm_algo_linear_resampler_q15_reset(&lrs15_st, bm_algo_float_to_q15(2.0f), 0);
    lrs_ok = bm_algo_linear_resampler_q15_step(&lrs15_st, BM_ALGO_Q15_ONE,
        s_batch14_lrs15_out, 4u, &lrs_count);
    TEST_ASSERT_TRUE(lrs_ok >= 0);

    bm_algo_linear_resampler_q31_reset(&lrs31_st, bm_algo_float_to_q31(2.0f), 0);
    lrs_ok = bm_algo_linear_resampler_q31_step(&lrs31_st, BM_ALGO_Q31_ONE,
        s_batch14_lrs31_out, 4u, &lrs_count);
    TEST_ASSERT_TRUE(lrs_ok >= 0);

    bm_algo_madgwick_q15_reset(&madg15_st);
    bm_algo_madgwick_q15_step(&madg15_st, &madg15_cfg, 0, 0, 0,
        bm_algo_float_to_q15(0.0f), bm_algo_float_to_q15(0.0f), BM_ALGO_Q15_ONE,
        bm_algo_float_to_q15(0.01f));
    TEST_ASSERT_TRUE(madg15_st.qw_q15 > 0);

    bm_algo_madgwick_q31_reset(&madg31_st);
    bm_algo_madgwick_q31_step(&madg31_st, &madg31_cfg, 0, 0, 0,
        bm_algo_float_to_q31(0.0f), bm_algo_float_to_q31(0.0f), BM_ALGO_Q31_ONE,
        bm_algo_float_to_q31(0.01f));
    TEST_ASSERT_TRUE(madg31_st.qw_q31 > 0);

    bm_algo_mahony_q15_reset(&mah15_st);
    bm_algo_mahony_q15_step(&mah15_st, &mah15_cfg, 0, 0, 0,
        bm_algo_float_to_q15(0.0f), bm_algo_float_to_q15(0.0f), BM_ALGO_Q15_ONE,
        bm_algo_float_to_q15(0.01f));
    TEST_ASSERT_TRUE(mah15_st.qw_q15 > 0);

    bm_algo_mahony_q31_reset(&mah31_st);
    bm_algo_mahony_q31_step(&mah31_st, &mah31_cfg, 0, 0, 0,
        bm_algo_float_to_q31(0.0f), bm_algo_float_to_q31(0.0f), BM_ALGO_Q31_ONE,
        bm_algo_float_to_q31(0.01f));
    TEST_ASSERT_TRUE(mah31_st.qw_q31 > 0);

    bm_algo_median_q15_reset(&med15_st);
    TEST_ASSERT_EQUAL(BM_ALGO_Q15_ONE,
        bm_algo_median_q15_step(&med15_st, &med15_cfg, BM_ALGO_Q15_ONE));

    bm_algo_median_q31_reset(&med31_st);
    TEST_ASSERT_EQUAL(BM_ALGO_Q31_ONE,
        bm_algo_median_q31_step(&med31_st, &med31_cfg, BM_ALGO_Q31_ONE));

    bm_algo_mppt_ic_q31_reset(&ic31_st, bm_algo_float_to_q31(0.4f));
    (void)bm_algo_mppt_ic_q31_step(&ic31_st, &ic31_cfg,
        bm_algo_float_to_q31(0.41f), bm_algo_float_to_q31(0.5f));
    TEST_ASSERT_TRUE(ic31_st.v_ref_q31 > 0);

    bm_algo_mppt_po_q31_reset(&po31_st, bm_algo_float_to_q31(0.4f));
    (void)bm_algo_mppt_po_q31_step(&po31_st, &po31_cfg,
        bm_algo_float_to_q31(0.41f), bm_algo_float_to_q31(0.5f));
    TEST_ASSERT_TRUE(po31_st.v_ref_q31 > 0);

    bm_algo_pid2_q31_reset(&pid231_st, 0);
    pid231_out = bm_algo_pid2_q31_step(&pid231_st, &pid231_cfg,
        bm_algo_float_to_q31(0.5f), bm_algo_float_to_q31(0.2f),
        bm_algo_float_to_q31(0.01f));
    TEST_ASSERT_TRUE(pid231_out > 0);

    bm_algo_range_monitor_q31_reset(&mon31_st, bm_algo_float_to_q31(0.5f));
    mon_flags = bm_algo_range_monitor_q31_step(&mon31_st, &mon31_cfg,
        bm_algo_float_to_q31(0.6f), bm_algo_float_to_q31(0.01f));
    TEST_ASSERT_NOT_EQUAL(0u, mon_flags & BM_ALGO_FAULT_OVER_RANGE);

    TEST_ASSERT_EQUAL(0, bm_algo_smith_predictor_q15_init(
        &smith15_st, &smith15_cfg, s_batch14_smith15_delay, 2u));
    smith15_err = bm_algo_smith_predictor_q15_step(&smith15_st, &smith15_cfg,
        bm_algo_float_to_q15(1.0f), bm_algo_float_to_q15(0.2f),
        bm_algo_float_to_q15(0.5f));
    TEST_ASSERT_TRUE(smith15_err != 0);

    TEST_ASSERT_EQUAL(0, bm_algo_smith_predictor_q31_init(
        &smith31_st, &smith31_cfg, s_batch14_smith31_delay, 2u));
    smith31_err = bm_algo_smith_predictor_q31_step(&smith31_st, &smith31_cfg,
        bm_algo_float_to_q31(1.0f), bm_algo_float_to_q31(0.2f),
        bm_algo_float_to_q31(0.5f));
    TEST_ASSERT_TRUE(smith31_err != 0);

    bm_algo_sogi_pll_q15_reset(&sogi15_st, &sogi15_cfg);
    bm_algo_sogi_pll_q15_step(&sogi15_st, &sogi15_cfg, BM_ALGO_Q15_ONE,
        bm_algo_float_to_q15(0.001f));
    TEST_ASSERT_TRUE(sogi15_st.omega_rad_s_q15 != 0);

    bm_algo_sogi_pll_q31_reset(&sogi31_st, &sogi31_cfg);
    bm_algo_sogi_pll_q31_step(&sogi31_st, &sogi31_cfg, BM_ALGO_Q31_ONE,
        bm_algo_float_to_q31(0.001f));
    TEST_ASSERT_TRUE(sogi31_st.omega_rad_s_q31 != 0);
}

static void test_fixed_batch14_smoke(void) {
    test_fixed_batch14_smoke_core();
    test_fixed_batch14_smoke_fusion();
}

static void test_batch14_fixed_batch13_and_refs(void) {
    test_ref_median_q15_golden();
    test_ref_pid2_q15_golden();
    test_ref_mppt_po_q31_golden();
    test_fixed_batch14_smoke();
}

/*
 * 全库审查修复回归（第二批：定点数学 bug）
 * 覆盖 P0-1 梯形刹车距离、P1-2 能量标度、P1-3 背隙双向偏移、P1-13 pid_q15 抗饱和。
 */
static void test_review_batch2_trapezoid_q31_vs_float(void) {
    bm_algo_trapezoid_config_t fcfg = {
        .max_vel = 0.5f, .max_accel = 0.1f, .max_decel = 0.1f
    };
    bm_algo_trapezoid_state_t fst;
    bm_algo_trapezoid_q31_config_t qcfg;
    bm_algo_trapezoid_q31_state_t qst;
    bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(0.05f);
    float dt_s = 0.05f;
    float target = 0.8f;
    float max_err = 0.0f;
    float max_overshoot = 0.0f;
    int i;

    qcfg.max_vel_q31 = bm_algo_float_to_q31(0.5f);
    qcfg.max_accel_q31 = bm_algo_float_to_q31(0.1f);
    qcfg.max_decel_q31 = bm_algo_float_to_q31(0.1f);

    bm_algo_trapezoid_reset(&fst, 0.0f, 0.0f);
    bm_algo_trapezoid_set_target(&fst, target);
    bm_algo_trapezoid_q31_reset(&qst, 0, 0);
    bm_algo_trapezoid_q31_set_target(&qst, bm_algo_float_to_q31(target));

    for (i = 0; i < 120; ++i) {
        float fp = bm_algo_trapezoid_step(&fst, &fcfg, dt_s);
        float qp = bm_algo_q31_to_float(
            bm_algo_trapezoid_q31_step(&qst, &qcfg, dt_q31));
        float err = fp - qp;
        if (err < 0.0f) {
            err = -err;
        }
        if (err > max_err) {
            max_err = err;
        }
        if (qp - target > max_overshoot) {
            max_overshoot = qp - target;
        }
    }

    /* 定点版与 float 参考逐步跟踪一致（累积舍入误差在合理范围内）。 */
    TEST_ASSERT_TRUE(max_err < 0.02f);
    /* 刹车距离修复后不应大幅过冲（旧 bug 下 stop_dist≈0 会过冲后振荡）。 */
    TEST_ASSERT_TRUE(max_overshoot < 0.02f);
    /* 两版均应收敛到目标。 */
    TEST_ASSERT_TRUE(fst.done != 0);
    TEST_ASSERT_TRUE(qst.done != 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, target, bm_algo_q31_to_float(qst.position));
}

static void test_review_batch2_energy_scale_consistency(void) {
    bm_algo_energy_wh_q15_state_t s15;
    bm_algo_energy_wh_q31_state_t s31;
    bm_algo_q15_t p_q15 = bm_algo_float_to_q15(0.5f);
    bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(0.5f);
    bm_algo_q31_t p_q31 = bm_algo_float_to_q31(0.5f);
    bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(0.5f);
    bm_algo_q31_t acc15 = 0;
    bm_algo_q31_t acc31 = 0;
    int64_t diff;
    int i;

    bm_algo_energy_wh_q15_reset(&s15);
    bm_algo_energy_wh_q31_reset(&s31);
    for (i = 0; i < 10; ++i) {
        acc15 = bm_algo_energy_wh_integrator_q15_step(&s15, p_q15, dt_q15);
        acc31 = bm_algo_energy_wh_integrator_q31_step(&s31, p_q31, dt_q31);
    }

    /* 相同物理功率×步长下两版累计能量（同为 Q31 定标）应一致。 */
    TEST_ASSERT_TRUE(acc15 > 0);
    TEST_ASSERT_TRUE(acc31 > 0);
    diff = (int64_t)acc15 - (int64_t)acc31;
    if (diff < 0) {
        diff = -diff;
    }
    TEST_ASSERT_TRUE(diff < 64);
}

static void test_review_batch2_backlash_q31_vs_float(void) {
    bm_algo_backlash_state_t fst;
    bm_algo_backlash_q31_state_t qst;
    float width = 0.2f;
    float slope = 0.05f;
    bm_algo_q31_t wq = bm_algo_float_to_q31(width);
    bm_algo_q31_t sq = bm_algo_float_to_q31(slope);
    const float cmds[6] = { 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f };
    int i;

    bm_algo_backlash_reset(&fst);
    bm_algo_backlash_q31_reset(&qst);

    for (i = 0; i < 6; ++i) {
        float fo = bm_algo_backlash_inverse(cmds[i], &fst, width, slope);
        float qo = bm_algo_q31_to_float(bm_algo_backlash_inverse_q31(
            bm_algo_float_to_q31(cmds[i]), &qst, wq, sq));
        TEST_ASSERT_FLOAT_WITHIN(0.001f, fo, qo);
    }

    /* 双向独立偏移：正向 4 次调用渐进到 width=0.2，反向 2 次调用累到 0.1，
     * 证明两方向各自累加、换向不清零（float v1.3 语义已在逐步比对中对齐）。 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, width, bm_algo_q31_to_float(qst.offset_fwd));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f * slope,
                             bm_algo_q31_to_float(qst.offset_rev));
}

static void test_review_batch2_pid_q15_antiwindup_no_overflow(void) {
    bm_algo_pid_q15_config_t cfg;
    bm_algo_pid_q15_state_t st;
    bm_algo_q15_t out;

    /* 构造 kp=error=-32768 使 p_term=+32768（越 int16）。旧代码把 p_term
     * 强转 Q15 会翻号成 -32768，令反算积分器符号错误（clamp 到正上限）；
     * int64 修复后反算得负值。out_max 收窄以确保抗饱和分支触发。 */
    memset(&cfg, 0, sizeof(cfg));
    cfg.kp = (bm_algo_q15_t)-32768;
    cfg.ki = 8192;
    cfg.kd = 0;
    cfg.d_filter_alpha_q15 = 0;
    cfg.integrator_min = -32767;
    cfg.integrator_max = 32767;
    cfg.out_min = -16384;
    cfg.out_max = 16384;

    bm_algo_pid_q15_reset(&st, 0);
    out = bm_algo_pid_q15_step(&st, &cfg, (bm_algo_q15_t)-32768,
                               BM_ALGO_Q15_ONE);

    /* 输出饱和到收窄的 out_max。 */
    TEST_ASSERT_EQUAL_INT(16384, (int)out);
    /* 反算积分器必须为负（旧 bug 会因强转翻号得到正上限 +32767）。 */
    TEST_ASSERT_TRUE(st.integrator < 0);
    TEST_ASSERT_TRUE(st.integrator >= -32767 && st.integrator <= 32767);
}

void test_algorithm(void) {
    RUN_TEST(test_review_batch2_trapezoid_q31_vs_float);
    RUN_TEST(test_review_batch2_energy_scale_consistency);
    RUN_TEST(test_review_batch2_backlash_q31_vs_float);
    RUN_TEST(test_review_batch2_pid_q15_antiwindup_no_overflow);
    RUN_TEST(test_common_clamp_and_deadband);
    RUN_TEST(test_pi_step_and_saturation);
    RUN_TEST(test_lpf1_step);
    RUN_TEST(test_hpf1_uses_high_pass_coefficient);
    RUN_TEST(test_ramp_reaches_target);
    RUN_TEST(test_scurve_reaches_target);
    RUN_TEST(test_clarke_park_roundtrip);
    RUN_TEST(test_coulomb_soc);
    RUN_TEST(test_rfft_execute);
    RUN_TEST(test_single_point_windows_are_finite);
    RUN_TEST(test_image_label_merges_connected_pixels);
    RUN_TEST(test_linear_resampler_ratio_and_capacity);
    RUN_TEST(test_goertzel_accepts_readonly_config);
    RUN_TEST(test_ekf_covariance_stays_symmetric);
    RUN_TEST(test_ukf1d_identity_tracks_measurement);
    RUN_TEST(test_ukf1d_square_model_updates);
    RUN_TEST(test_ekf_gate_and_gated_update);
    RUN_TEST(test_imu_calib_apply_and_accumulator);
    RUN_TEST(test_stft_overlap_emits_hop_frames);
    RUN_TEST(test_mahony_uses_simultaneous_quaternion_update);
    RUN_TEST(test_sogi_states_decay_after_input_stops);
    RUN_TEST(test_fixed_pi_q31_saturates);
    RUN_TEST(test_fixed_lpf1_q15_tracks_input);
    RUN_TEST(test_fixed_point_saturates_before_narrowing);
    RUN_TEST(test_fixed_point_negative_full_scale_product_saturates);
    RUN_TEST(test_motion_and_profile_boundary_regressions);
    RUN_TEST(test_motor_voltage_scaling_and_deadtime);
    RUN_TEST(test_numeric_guard_regressions);
    RUN_TEST(test_soc_and_image_boundary_regressions);
    RUN_TEST(test_runtime_buffer_config_changes_are_rejected);
    RUN_TEST(test_flux_observer_and_mtpa);
    RUN_TEST(test_battery_temp_and_motor_extras);
    RUN_TEST(test_zero_length_audio_is_ignored);
    RUN_TEST(test_vision_centroid_and_compensation);
    RUN_TEST(test_soc_ekf_and_power_quality);
    RUN_TEST(test_p1_k0_and_fixed_extensions);
    RUN_TEST(test_detection_matched_and_ultrasonic);
    RUN_TEST(test_matched_filter_accepts_negative_correlations);
    RUN_TEST(test_w2_audio_spectral_motion);
    RUN_TEST(test_eq_stepper_and_smith_regressions);
    RUN_TEST(test_review_fixes);
    RUN_TEST(test_batch3_k0_extensions);
    RUN_TEST(test_batch4_k0_and_fixed_batch3);
    RUN_TEST(test_batch5_k0_and_fixed_batch4);
    RUN_TEST(test_batch6_fixed_batch5);
    RUN_TEST(test_batch7_k0_and_fixed_batch6);
    RUN_TEST(test_batch8_fixed_batch7_and_refs);
    RUN_TEST(test_batch9_fixed_batch8_and_refs);
    RUN_TEST(test_batch10_fixed_batch9_and_refs);
    RUN_TEST(test_batch11_fixed_batch10_and_refs);
    RUN_TEST(test_batch12_fixed_batch11_and_refs);
    RUN_TEST(test_batch13_fixed_batch12_and_refs);
    RUN_TEST(test_batch14_fixed_batch13_and_refs);
}

int main(void) {
    UNITY_BEGIN();
    test_algorithm();
    return UNITY_END();
}

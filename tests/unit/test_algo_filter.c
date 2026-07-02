/**
 * @file test_algo_filter.c
 * @brief bm_algorithm 滤波/DSP（LPF/HPF/FFT/窗函数/重采样/Goertzel/STFT/SOGI/匹配滤波）单元测试
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

static const bm_algo_goertzel_config_t s_readonly_goertzel_config = {
    .target_freq_hz = 100.0f,
    .sample_hz = 1000.0f,
    .block_size = 20u,
    .coeff = 0.0f
};

static void test_goertzel_accepts_readonly_config(void) {
    bm_algo_goertzel_state_t st;

    TEST_ASSERT_EQUAL(0, bm_algo_goertzel_init(
        &st, &s_readonly_goertzel_config));
    TEST_ASSERT_TRUE(fabsf(st.coeff) > 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(
        0.0f, 0.0f, s_readonly_goertzel_config.coeff);
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

static void test_matched_filter_accepts_negative_correlations(void) {
    const float signal[2] = { -2.0f, -3.0f };
    const float template_data[1] = { 1.0f };
    uint32_t index = UINT32_MAX;

    TEST_ASSERT_FLOAT_WITHIN(
        0.001f, -2.0f,
        bm_algo_matched_filter(signal, 2u, template_data, 1u, &index));
    TEST_ASSERT_EQUAL_UINT32(0u, index);
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

void test_algo_filter(void) {
    RUN_TEST(test_lpf1_step);
    RUN_TEST(test_hpf1_uses_high_pass_coefficient);
    RUN_TEST(test_rfft_execute);
    RUN_TEST(test_single_point_windows_are_finite);
    RUN_TEST(test_linear_resampler_ratio_and_capacity);
    RUN_TEST(test_goertzel_accepts_readonly_config);
    RUN_TEST(test_stft_overlap_emits_hop_frames);
    RUN_TEST(test_sogi_states_decay_after_input_stops);
    RUN_TEST(test_matched_filter_accepts_negative_correlations);
    RUN_TEST(test_eq_stepper_and_smith_regressions);
}

int main(void) {
    UNITY_BEGIN();
    test_algo_filter();
    return UNITY_END();
}

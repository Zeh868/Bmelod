/**
 * @file test_algo_fixed.c
 * @brief bm_algorithm 定点 Q15/Q31 批次黄金向量 + 批②对照 + 双轨（float vs Q）对照 12 族单元测试
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

/*
 * ==========================================================================
 * Phase 1 Task 1：双轨算法对照测试网（12 族 float vs Q15/Q31 逐步对照）
 *
 * 参见 docs/superpowers/plans/2026-07-02-arch-improvement-schedule.md。
 * Q15/Q31 版当前均经 float 桥接实现（见 bm_algo_fixed.h @warning），
 * 逐步对照容差：Q31 0.01f、Q15 0.02f；真定点化后本组测试是回归门。
 * ==========================================================================
 */

/**
 * @brief 双轨对照：differentiator Q31/Q15 与 float 版逐步一致性
 *
 * 幅值/频率选取保证微分量级 |dx/dt| 明显小于 1.0，避免 Q31/Q15 输出饱和。
 */
static void test_dualtrack_differentiator_q_vs_float(void) {
    bm_algo_differentiator_config_t     fcfg = { .coeff = 0.3f };
    bm_algo_differentiator_state_t      fst;
    bm_algo_differentiator_q31_config_t q31cfg;
    bm_algo_differentiator_q31_state_t  q31st;
    bm_algo_differentiator_q15_config_t q15cfg;
    bm_algo_differentiator_q15_state_t  q15st;
    const float dt_s = 0.05f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    int i;

    q31cfg.coeff_q31 = bm_algo_float_to_q31(0.3f);
    q15cfg.coeff_q15 = bm_algo_float_to_q15(0.3f);

    bm_algo_differentiator_reset(&fst);
    bm_algo_differentiator_q31_reset(&q31st);
    bm_algo_differentiator_q15_reset(&q15st);

    for (i = 0; i < 64; ++i) {
        float x = 0.3f * sinf((float)i * 0.05f);
        float f_out = bm_algo_differentiator_step(&fst, &fcfg, x, dt_s);
        float q31_out = bm_algo_q31_to_float(bm_algo_differentiator_q31_step(
            &q31st, &q31cfg, bm_algo_float_to_q31(x), dt_q31));
        float q15_out = bm_algo_q15_to_float(bm_algo_differentiator_q15_step(
            &q15st, &q15cfg, bm_algo_float_to_q15(x), dt_q15));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, f_out, q31_out);
        TEST_ASSERT_FLOAT_WITHIN(0.02f, f_out, q15_out);
    }
}

/**
 * @brief 双轨对照：smith_predictor Q31/Q15 与 float 版逐步一致性
 *
 * delay_steps=3，u_controller 幅值 <1 保证 model_gain*u 不越界。
 */
static void test_dualtrack_smith_predictor_q_vs_float(void) {
    bm_algo_smith_predictor_config_t     fcfg = { .model_gain = 0.5f, .delay_steps = 3u };
    bm_algo_smith_predictor_state_t      fst;
    float                                f_delay[3];
    bm_algo_smith_predictor_q31_config_t q31cfg;
    bm_algo_smith_predictor_q31_state_t  q31st;
    bm_algo_q31_t                        q31_delay[3];
    bm_algo_smith_predictor_q15_config_t q15cfg;
    bm_algo_smith_predictor_q15_state_t  q15st;
    bm_algo_q15_t                        q15_delay[3];
    int i;

    q31cfg.model_gain_q31 = bm_algo_float_to_q31(0.5f);
    q31cfg.delay_steps = 3u;
    q15cfg.model_gain_q15 = bm_algo_float_to_q15(0.5f);
    q15cfg.delay_steps = 3u;

    TEST_ASSERT_EQUAL(0, bm_algo_smith_predictor_init(&fst, &fcfg, f_delay, 3u));
    TEST_ASSERT_EQUAL(0, bm_algo_smith_predictor_q31_init(&q31st, &q31cfg, q31_delay, 3u));
    TEST_ASSERT_EQUAL(0, bm_algo_smith_predictor_q15_init(&q15st, &q15cfg, q15_delay, 3u));

    for (i = 0; i < 40; ++i) {
        float reference = 0.5f;
        float measurement = 0.5f;
        float u_controller = 0.3f * sinf((float)i * 0.1f);
        float f_err = bm_algo_smith_predictor_step(&fst, &fcfg, reference, measurement, u_controller);
        float q31_err = bm_algo_q31_to_float(bm_algo_smith_predictor_q31_step(
            &q31st, &q31cfg, bm_algo_float_to_q31(reference),
            bm_algo_float_to_q31(measurement), bm_algo_float_to_q31(u_controller)));
        float q15_err = bm_algo_q15_to_float(bm_algo_smith_predictor_q15_step(
            &q15st, &q15cfg, bm_algo_float_to_q15(reference),
            bm_algo_float_to_q15(measurement), bm_algo_float_to_q15(u_controller)));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, f_err, q31_err);
        TEST_ASSERT_FLOAT_WITHIN(0.02f, f_err, q15_err);
    }
}

/**
 * @brief 双轨对照：moving_avg Q31/Q15 与 float 版逐步一致性（window=5）
 */
static void test_dualtrack_moving_avg_q_vs_float(void) {
    float                            fbuf[5];
    bm_algo_moving_avg_config_t      fcfg = { .buffer = fbuf, .length = 5u };
    bm_algo_moving_avg_state_t       fst;
    bm_algo_moving_avg_q31_config_t  q31cfg = { .window_size = 5u };
    bm_algo_moving_avg_q31_state_t   q31st;
    bm_algo_moving_avg_q15_config_t  q15cfg = { .window_size = 5u };
    bm_algo_moving_avg_q15_state_t   q15st;
    int i;

    TEST_ASSERT_EQUAL(0, bm_algo_moving_avg_init(&fst, &fcfg));
    bm_algo_moving_avg_q31_reset(&q31st);
    bm_algo_moving_avg_q15_reset(&q15st);

    for (i = 0; i < 30; ++i) {
        float x = 0.4f * sinf((float)i * 0.15f);
        float f_out = bm_algo_moving_avg_step(&fst, &fcfg, x);
        float q31_out = bm_algo_q31_to_float(bm_algo_moving_avg_q31_step(
            &q31st, &q31cfg, bm_algo_float_to_q31(x)));
        float q15_out = bm_algo_q15_to_float(bm_algo_moving_avg_q15_step(
            &q15st, &q15cfg, bm_algo_float_to_q15(x)));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, f_out, q31_out);
        TEST_ASSERT_FLOAT_WITHIN(0.02f, f_out, q15_out);
    }
}

/**
 * @brief 双轨对照：complementary（互补滤波）Q31/Q15 与 float 版逐步一致性
 *
 * 比较 roll_rad/pitch_rad 姿态分量。
 */
static void test_dualtrack_complementary_q_vs_float(void) {
    bm_algo_complementary_config_t     fcfg = { .alpha = 0.98f };
    bm_algo_complementary_state_t      fst;
    bm_algo_complementary_q31_config_t q31cfg = { .alpha_q31 = bm_algo_float_to_q31(0.98f) };
    bm_algo_complementary_q31_state_t  q31st;
    bm_algo_complementary_q15_config_t q15cfg = { .alpha_q15 = bm_algo_float_to_q15(0.98f) };
    bm_algo_complementary_q15_state_t  q15st;
    const float dt_s = 0.01f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    int i;

    bm_algo_complementary_reset(&fst);
    bm_algo_complementary_q31_reset(&q31st);
    bm_algo_complementary_q15_reset(&q15st);

    for (i = 0; i < 40; ++i) {
        float gx = 0.1f * sinf((float)i * 0.2f);
        float gy = 0.05f;

        bm_algo_complementary_step(&fst, &fcfg, gx, gy, 0.0f, 0.0f, 0.0f, 1.0f, dt_s);
        bm_algo_complementary_q31_step(&q31st, &q31cfg,
            bm_algo_float_to_q31(gx), bm_algo_float_to_q31(gy), 0,
            0, 0, BM_ALGO_Q31_ONE, dt_q31);
        bm_algo_complementary_q15_step(&q15st, &q15cfg,
            bm_algo_float_to_q15(gx), bm_algo_float_to_q15(gy), 0,
            0, 0, BM_ALGO_Q15_ONE, dt_q15);

        TEST_ASSERT_FLOAT_WITHIN(0.01f, fst.roll_rad, bm_algo_q31_to_float(q31st.roll_rad));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, fst.pitch_rad, bm_algo_q31_to_float(q31st.pitch_rad));
        TEST_ASSERT_FLOAT_WITHIN(0.02f, fst.roll_rad, bm_algo_q15_to_float(q15st.roll_rad));
        TEST_ASSERT_FLOAT_WITHIN(0.02f, fst.pitch_rad, bm_algo_q15_to_float(q15st.pitch_rad));
    }
}

/**
 * @brief 双轨对照：Mahony AHRS Q31/Q15 与 float 版姿态分量一致性
 *
 * 无磁力计观测时 yaw 不可观测（漂移方向对输入路径敏感），仅比较
 * roll/pitch（经 bm_algo_quat_to_euler 从四元数换算）。
 */
static void test_dualtrack_mahony_q_vs_float(void) {
    bm_algo_mahony_config_t     fcfg = { .kp = 2.0f, .ki = 0.01f };
    bm_algo_mahony_state_t      fst;
    bm_algo_euler_t             f_euler;
    bm_algo_mahony_q31_config_t q31cfg;
    bm_algo_mahony_q31_state_t  q31st;
    bm_algo_quat_t              q31_quat;
    bm_algo_euler_t             q31_euler;
    bm_algo_mahony_q15_config_t q15cfg;
    bm_algo_mahony_q15_state_t  q15st;
    bm_algo_quat_t              q15_quat;
    bm_algo_euler_t             q15_euler;
    const float dt_s = 0.01f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    int i;

    q31cfg.kp_q31 = bm_algo_float_to_q31(2.0f);
    q31cfg.ki_q31 = bm_algo_float_to_q31(0.01f);
    q15cfg.kp_q15 = bm_algo_float_to_q15(2.0f);
    q15cfg.ki_q15 = bm_algo_float_to_q15(0.01f);

    bm_algo_mahony_reset(&fst);
    bm_algo_mahony_q31_reset(&q31st);
    bm_algo_mahony_q15_reset(&q15st);

    for (i = 0; i < 30; ++i) {
        float gx = 0.05f * sinf((float)i * 0.2f);
        float gy = 0.03f;

        bm_algo_mahony_step(&fst, &fcfg, gx, gy, 0.0f, 0.0f, 0.0f, 1.0f, dt_s);
        bm_algo_mahony_q31_step(&q31st, &q31cfg,
            bm_algo_float_to_q31(gx), bm_algo_float_to_q31(gy), 0,
            0, 0, BM_ALGO_Q31_ONE, dt_q31);
        bm_algo_mahony_q15_step(&q15st, &q15cfg,
            bm_algo_float_to_q15(gx), bm_algo_float_to_q15(gy), 0,
            0, 0, BM_ALGO_Q15_ONE, dt_q15);
    }

    bm_algo_quat_to_euler(&fst.q, &f_euler);
    q31_quat.w = bm_algo_q31_to_float(q31st.qw_q31);
    q31_quat.x = bm_algo_q31_to_float(q31st.qx_q31);
    q31_quat.y = bm_algo_q31_to_float(q31st.qy_q31);
    q31_quat.z = bm_algo_q31_to_float(q31st.qz_q31);
    bm_algo_quat_to_euler(&q31_quat, &q31_euler);
    q15_quat.w = bm_algo_q15_to_float(q15st.qw_q15);
    q15_quat.x = bm_algo_q15_to_float(q15st.qx_q15);
    q15_quat.y = bm_algo_q15_to_float(q15st.qy_q15);
    q15_quat.z = bm_algo_q15_to_float(q15st.qz_q15);
    bm_algo_quat_to_euler(&q15_quat, &q15_euler);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, f_euler.roll_rad, q31_euler.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f_euler.pitch_rad, q31_euler.pitch_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, f_euler.roll_rad, q15_euler.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, f_euler.pitch_rad, q15_euler.pitch_rad);
}

/**
 * @brief 双轨对照：Madgwick AHRS Q31/Q15 与 float 版姿态分量一致性
 *
 * 同 Mahony：无磁力计时 yaw 不可观测，仅比较 roll/pitch。
 */
static void test_dualtrack_madgwick_q_vs_float(void) {
    bm_algo_madgwick_config_t     fcfg = { .beta = 0.1f };
    bm_algo_madgwick_state_t      fst;
    bm_algo_euler_t               f_euler;
    bm_algo_madgwick_q31_config_t q31cfg = { .beta_q31 = bm_algo_float_to_q31(0.1f) };
    bm_algo_madgwick_q31_state_t  q31st;
    bm_algo_quat_t                q31_quat;
    bm_algo_euler_t               q31_euler;
    bm_algo_madgwick_q15_config_t q15cfg = { .beta_q15 = bm_algo_float_to_q15(0.1f) };
    bm_algo_madgwick_q15_state_t  q15st;
    bm_algo_quat_t                q15_quat;
    bm_algo_euler_t               q15_euler;
    const float dt_s = 0.01f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    int i;

    bm_algo_madgwick_reset(&fst);
    bm_algo_madgwick_q31_reset(&q31st);
    bm_algo_madgwick_q15_reset(&q15st);

    for (i = 0; i < 30; ++i) {
        float gx = 0.05f * sinf((float)i * 0.2f);
        float gy = 0.03f;

        bm_algo_madgwick_step(&fst, &fcfg, gx, gy, 0.0f, 0.0f, 0.0f, 1.0f, dt_s);
        bm_algo_madgwick_q31_step(&q31st, &q31cfg,
            bm_algo_float_to_q31(gx), bm_algo_float_to_q31(gy), 0,
            0, 0, BM_ALGO_Q31_ONE, dt_q31);
        bm_algo_madgwick_q15_step(&q15st, &q15cfg,
            bm_algo_float_to_q15(gx), bm_algo_float_to_q15(gy), 0,
            0, 0, BM_ALGO_Q15_ONE, dt_q15);
    }

    bm_algo_quat_to_euler(&fst.q, &f_euler);
    q31_quat.w = bm_algo_q31_to_float(q31st.qw_q31);
    q31_quat.x = bm_algo_q31_to_float(q31st.qx_q31);
    q31_quat.y = bm_algo_q31_to_float(q31st.qy_q31);
    q31_quat.z = bm_algo_q31_to_float(q31st.qz_q31);
    bm_algo_quat_to_euler(&q31_quat, &q31_euler);
    q15_quat.w = bm_algo_q15_to_float(q15st.qw_q15);
    q15_quat.x = bm_algo_q15_to_float(q15st.qx_q15);
    q15_quat.y = bm_algo_q15_to_float(q15st.qy_q15);
    q15_quat.z = bm_algo_q15_to_float(q15st.qz_q15);
    bm_algo_quat_to_euler(&q15_quat, &q15_euler);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, f_euler.roll_rad, q31_euler.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f_euler.pitch_rad, q31_euler.pitch_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, f_euler.roll_rad, q15_euler.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, f_euler.pitch_rad, q15_euler.pitch_rad);
}

/**
 * @brief 双轨对照：DDA 直线插补 Q31/Q15 与 float 版逐步一致性
 *
 * 先断言 done/返回状态一致，再比较坐标输出（int 状态族）。
 */
static void test_dualtrack_dda_q_vs_float(void) {
    bm_algo_dda_config_t     fcfg = { 0.0f, 0.0f, 0.5f, 0.3f, 0.05f };
    bm_algo_dda_state_t      fst;
    bm_algo_dda_q31_config_t q31cfg;
    bm_algo_dda_q31_state_t  q31st;
    bm_algo_dda_q15_config_t q15cfg;
    bm_algo_dda_q15_state_t  q15st;
    int i;

    q31cfg.x0_q31 = bm_algo_float_to_q31(0.0f);
    q31cfg.y0_q31 = bm_algo_float_to_q31(0.0f);
    q31cfg.x1_q31 = bm_algo_float_to_q31(0.5f);
    q31cfg.y1_q31 = bm_algo_float_to_q31(0.3f);
    q31cfg.step_size_q31 = bm_algo_float_to_q31(0.05f);

    q15cfg.x0_q15 = bm_algo_float_to_q15(0.0f);
    q15cfg.y0_q15 = bm_algo_float_to_q15(0.0f);
    q15cfg.x1_q15 = bm_algo_float_to_q15(0.5f);
    q15cfg.y1_q15 = bm_algo_float_to_q15(0.3f);
    q15cfg.step_size_q15 = bm_algo_float_to_q15(0.05f);

    bm_algo_dda_reset(&fst, &fcfg);
    bm_algo_dda_q31_reset(&q31st, &q31cfg);
    bm_algo_dda_q15_reset(&q15st, &q15cfg);

    for (i = 0; i < 20; ++i) {
        float x_f = 0.0f, y_f = 0.0f;
        bm_algo_q31_t x_q31 = 0, y_q31 = 0;
        bm_algo_q15_t x_q15 = 0, y_q15 = 0;
        int f_ok, q31_ok, q15_ok;

        f_ok = bm_algo_dda_step(&fst, &fcfg, &x_f, &y_f);
        q31_ok = bm_algo_dda_q31_step(&q31st, &q31cfg, &x_q31, &y_q31);
        q15_ok = bm_algo_dda_q15_step(&q15st, &q15cfg, &x_q15, &y_q15);

        TEST_ASSERT_EQUAL_INT(f_ok, q31_ok);
        TEST_ASSERT_EQUAL_INT(f_ok, q15_ok);
        if (!f_ok) {
            break;
        }
        TEST_ASSERT_FLOAT_WITHIN(0.01f, x_f, bm_algo_q31_to_float(x_q31));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, y_f, bm_algo_q31_to_float(y_q31));
        TEST_ASSERT_FLOAT_WITHIN(0.02f, x_f, bm_algo_q15_to_float(x_q15));
        TEST_ASSERT_FLOAT_WITHIN(0.02f, y_f, bm_algo_q15_to_float(y_q15));
    }
    TEST_ASSERT_TRUE(fst.done != 0);
    TEST_ASSERT_TRUE(q31st.done != 0);
    TEST_ASSERT_TRUE(q15st.done != 0);
}

/**
 * @brief 双轨对照：SOGI-PLL Q31/Q15 与 float 版趋势一致性（降级）
 *
 * 降级原因（审查中发现的 Source 缺陷，其中未初始化读已修复，
 * 定标域限制为 ABI 约束不在本轮修复范围）：
 * 1) 【未修复，ABI 限制】bm_algo_sogi_pll_q31/q15_config_t 无
 *    integrator_limit_ratio 字段，只能表达
 *    nominal_omega_rad_s/k_sogi/k_pll；真实电网角频率
 *    （如 2π×50≈314 rad/s）远超 Q31/Q15 的 ±1.0 定标域，
 *    bm_algo_float_to_q31/q15() 会直接饱和到 1.0，Q 版与 float 版
 *    config 无法表达同一物理量，逐步数值对照失去意义。
 * 2) 【已修复】bm_algo_fixed.c 中 bm_algo_sogi_pll_q15/q31_reset()/_step()
 *    桥接到 float 版时，局部 fcfg 曾只赋值 nominal_omega_rad_s/k_sogi/k_pll
 *    三个字段，未赋值 bm_algo_sogi_pll_config_t::integrator_limit_ratio
 *    （未初始化读，UB）。现已显式置 0，复用 float 实现"0 时自动取 0.2"
 *    的既定默认限幅比语义（见 bm_algo_power.c 第 120~127 行），消除 UB。
 * 因缺陷 1) 仍在（ABI 限制无法表达真实物理角频率），本测试仍仅对三版本
 * 做"趋势一致性"断言（有限、theta 落在回绕域内），不做逐步数值比对；
 * config 使用可在 Q31/Q15 定标域内精确表达的归一化角频率（非真实物理 Hz），
 * 仅用于驱动桥接路径。
 */
static void test_dualtrack_sogi_pll_q_vs_float(void) {
    bm_algo_sogi_pll_config_t     fcfg = {
        .nominal_omega_rad_s = 0.5f, .k_sogi = 0.3f,
        .k_pll = 0.2f, .integrator_limit_ratio = 0.2f
    };
    bm_algo_sogi_pll_state_t      fst;
    bm_algo_sogi_pll_q31_config_t q31cfg;
    bm_algo_sogi_pll_q31_state_t  q31st;
    bm_algo_sogi_pll_q15_config_t q15cfg;
    bm_algo_sogi_pll_q15_state_t  q15st;
    const float dt_s = 0.01f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    int i;

    q31cfg.nominal_omega_q31 = bm_algo_float_to_q31(0.5f);
    q31cfg.k_sogi_q31 = bm_algo_float_to_q31(0.3f);
    q31cfg.k_pll_q31 = bm_algo_float_to_q31(0.2f);
    q15cfg.nominal_omega_q15 = bm_algo_float_to_q15(0.5f);
    q15cfg.k_sogi_q15 = bm_algo_float_to_q15(0.3f);
    q15cfg.k_pll_q15 = bm_algo_float_to_q15(0.2f);

    bm_algo_sogi_pll_reset(&fst, &fcfg);
    bm_algo_sogi_pll_q31_reset(&q31st, &q31cfg);
    bm_algo_sogi_pll_q15_reset(&q15st, &q15cfg);

    for (i = 0; i < 50; ++i) {
        float v_in = sinf((float)i * 0.05f);
        bm_algo_sogi_pll_step(&fst, &fcfg, v_in, dt_s);
        bm_algo_sogi_pll_q31_step(&q31st, &q31cfg, bm_algo_float_to_q31(v_in), dt_q31);
        bm_algo_sogi_pll_q15_step(&q15st, &q15cfg, bm_algo_float_to_q15(v_in), dt_q15);
    }

    TEST_ASSERT_TRUE(bm_algo_is_finite_f(fst.theta_rad));
    TEST_ASSERT_TRUE(bm_algo_is_finite_f(fst.omega_rad_s));
    TEST_ASSERT_TRUE(bm_algo_is_finite_f(q31st.theta_rad));
    TEST_ASSERT_TRUE(bm_algo_is_finite_f(q31st.omega_rad_s));
    TEST_ASSERT_TRUE(bm_algo_is_finite_f(q15st.theta_rad));
    TEST_ASSERT_TRUE(bm_algo_is_finite_f(q15st.omega_rad_s));

    /* theta 应落在 [0, 2π) 回绕域内（bm_algo_angle_wrap_0_2pi_rad 契约）。 */
    TEST_ASSERT_TRUE(fst.theta_rad >= 0.0f && fst.theta_rad < 6.29f);
    TEST_ASSERT_TRUE(q31st.theta_rad >= 0.0f && q31st.theta_rad < 6.29f);
    TEST_ASSERT_TRUE(q15st.theta_rad >= 0.0f && q15st.theta_rad < 6.29f);
}

/**
 * @brief 双轨对照：RMS（窗口方均根）Q31/Q15 与 float 版逐步一致性
 */
static void test_dualtrack_rms_q_vs_float(void) {
    bm_algo_rms_config_t     fcfg = { .window_samples = 8u };
    bm_algo_rms_state_t      fst;
    float                    fbuf[8];
    bm_algo_rms_q31_config_t q31cfg = { .window_size = 8u };
    bm_algo_rms_q31_state_t  q31st;
    bm_algo_rms_q15_config_t q15cfg = { .window_size = 8u };
    bm_algo_rms_q15_state_t  q15st;
    int i;

    TEST_ASSERT_EQUAL(0, bm_algo_rms_init(&fst, &fcfg, fbuf, 8u));
    bm_algo_rms_q31_reset(&q31st);
    bm_algo_rms_q15_reset(&q15st);

    for (i = 0; i < 20; ++i) {
        float x = 0.4f * sinf((float)i * 0.3f);
        float f_out = bm_algo_rms_step(&fst, &fcfg, x);
        float q31_out = bm_algo_q31_to_float(
            bm_algo_rms_q31_step(&q31st, &q31cfg, bm_algo_float_to_q31(x)));
        float q15_out = bm_algo_q15_to_float(
            bm_algo_rms_q15_step(&q15st, &q15cfg, bm_algo_float_to_q15(x)));
        TEST_ASSERT_TRUE(f_out >= 0.0f);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, f_out, q31_out);
        TEST_ASSERT_FLOAT_WITHIN(0.02f, f_out, q15_out);
    }
}

/**
 * @brief 双轨对照：flux_observer（磁链观测+PLL）Q31/Q15 与 float 版逐步一致性
 *
 * ls_h 置 0 避免磁链角在瞬态首步出现大跳变（|flux| 接近 0 时 atan2 数值
 * 敏感）；v/i 激励为恒定小幅值，使收敛角度落在 Q31/Q15 的 ±1.0 rad
 * 定标域内，避免角度输出饱和。
 */
static void test_dualtrack_flux_observer_q_vs_float(void) {
    bm_algo_flux_observer_config_t     fcfg = {
        .rs_ohm = 0.1f, .ls_h = 0.0f, .pll_kp = 0.5f, .pll_ki = 0.05f,
        .flux_observer_wc_rad_s = 10.0f
    };
    bm_algo_flux_observer_state_t      fst;
    bm_algo_flux_observer_q31_config_t q31cfg;
    bm_algo_flux_observer_q31_state_t  q31st;
    bm_algo_flux_observer_q15_config_t q15cfg;
    bm_algo_flux_observer_q15_state_t  q15st;
    const float dt_s = 0.001f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    const float v_alpha = 0.15f;
    const float v_beta = 0.03f;
    const float i_alpha = 0.05f;
    const float i_beta = 0.01f;
    int i;

    q31cfg.rs_q31 = bm_algo_float_to_q31(0.1f);
    q31cfg.ls_q31 = bm_algo_float_to_q31(0.0f);
    q31cfg.pll_kp_q31 = bm_algo_float_to_q31(0.5f);
    q31cfg.pll_ki_q31 = bm_algo_float_to_q31(0.05f);
    q31cfg.wc_rad_s = 10.0f;

    q15cfg.rs_q15 = bm_algo_float_to_q15(0.1f);
    q15cfg.ls_q15 = bm_algo_float_to_q15(0.0f);
    q15cfg.pll_kp_q15 = bm_algo_float_to_q15(0.5f);
    q15cfg.pll_ki_q15 = bm_algo_float_to_q15(0.05f);
    q15cfg.wc_rad_s = 10.0f;

    bm_algo_flux_observer_reset(&fst, 0.0f);
    bm_algo_flux_observer_q31_reset(&q31st, 0);
    bm_algo_flux_observer_q15_reset(&q15st, 0);

    for (i = 0; i < 30; ++i) {
        float f_theta = bm_algo_flux_observer_step(&fst, &fcfg,
            v_alpha, v_beta, i_alpha, i_beta, dt_s);
        float q31_theta = bm_algo_q31_to_float(bm_algo_flux_observer_q31_step(
            &q31st, &q31cfg,
            bm_algo_float_to_q31(v_alpha), bm_algo_float_to_q31(v_beta),
            bm_algo_float_to_q31(i_alpha), bm_algo_float_to_q31(i_beta), dt_q31));
        float q15_theta = bm_algo_q15_to_float(bm_algo_flux_observer_q15_step(
            &q15st, &q15cfg,
            bm_algo_float_to_q15(v_alpha), bm_algo_float_to_q15(v_beta),
            bm_algo_float_to_q15(i_alpha), bm_algo_float_to_q15(i_beta), dt_q15));

        TEST_ASSERT_FLOAT_WITHIN(0.01f, f_theta, q31_theta);
        TEST_ASSERT_FLOAT_WITHIN(0.02f, f_theta, q15_theta);
    }
}

/**
 * @brief 双轨对照：S 曲线轨迹 Q31/Q15 与 float 版趋势一致性（降级）
 *
 * 降级原因：bm_algo_scurve_step 用"预估制动距离 vs 剩余距离"的
 * bang-bang 阈值比较决定加速/减速相位切换（bm_algo_profile.c 第
 * 194~202 行）。bm_algo_scurve_q31/q15_step 每步都把 position/
 * velocity/acceleration 经 Q31/Q15 往返量化后再调用该 float 核心
 * （未保留跨步 float 影子状态，见 bm_algo_fixed.c 对应实现），量化引入
 * 的极小偏差可能使阈值比较在某一步上与 float 版判定不同，导致该步起
 * 两版进入不同的加速度相位，此后位置轨迹持续偏离（非收敛的一次性
 * 瞬态：实测偏差随迭代增长，非固定量级），逐步数值对照不稳定。降级为
 * 趋势一致性：三版本均应收敛（done）、收敛后位置落在各自定标域下的
 * target 附近，且收敛所需步数量级相近（非逐步数值比对）。
 */
static void test_dualtrack_scurve_q_vs_float(void) {
    bm_algo_scurve_config_t     fcfg = { .max_vel = 0.4f, .max_accel = 0.3f, .max_jerk = 3.0f };
    bm_algo_scurve_state_t      fst;
    bm_algo_scurve_q31_config_t q31cfg;
    bm_algo_scurve_q31_state_t  q31st;
    bm_algo_scurve_q15_config_t q15cfg;
    bm_algo_scurve_q15_state_t  q15st;
    const float dt_s = 0.02f;
    const bm_algo_q31_t dt_q31 = bm_algo_float_to_q31(dt_s);
    const bm_algo_q15_t dt_q15 = bm_algo_float_to_q15(dt_s);
    const float target = 0.5f;
    const int max_steps = 300;
    int f_steps = -1, q31_steps = -1, q15_steps = -1;
    int step_diff;
    int i;

    q31cfg.max_vel_q31 = bm_algo_float_to_q31(0.4f);
    q31cfg.max_accel_q31 = bm_algo_float_to_q31(0.3f);
    q31cfg.max_jerk_q31 = bm_algo_float_to_q31(3.0f);
    q15cfg.max_vel_q15 = bm_algo_float_to_q15(0.4f);
    q15cfg.max_accel_q15 = bm_algo_float_to_q15(0.3f);
    q15cfg.max_jerk_q15 = bm_algo_float_to_q15(3.0f);

    bm_algo_scurve_reset(&fst, 0.0f, 0.0f, 0.0f);
    bm_algo_scurve_set_target(&fst, target);
    bm_algo_scurve_q31_reset(&q31st, 0, 0, 0);
    bm_algo_scurve_q31_set_target(&q31st, bm_algo_float_to_q31(target));
    bm_algo_scurve_q15_reset(&q15st, 0, 0, 0);
    bm_algo_scurve_q15_set_target(&q15st, bm_algo_float_to_q15(target));

    for (i = 0; i < max_steps; ++i) {
        if (!fst.done) {
            (void)bm_algo_scurve_step(&fst, &fcfg, dt_s);
            if (fst.done && f_steps < 0) {
                f_steps = i + 1;
            }
        }
        if (!q31st.done) {
            (void)bm_algo_scurve_q31_step(&q31st, &q31cfg, dt_q31);
            if (q31st.done && q31_steps < 0) {
                q31_steps = i + 1;
            }
        }
        if (!q15st.done) {
            (void)bm_algo_scurve_q15_step(&q15st, &q15cfg, dt_q15);
            if (q15st.done && q15_steps < 0) {
                q15_steps = i + 1;
            }
        }
        if (fst.done && q31st.done && q15st.done) {
            break;
        }
    }

    TEST_ASSERT_TRUE(fst.done != 0);
    TEST_ASSERT_TRUE(q31st.done != 0);
    TEST_ASSERT_TRUE(q15st.done != 0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, target, fst.position);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, target, bm_algo_q31_to_float(q31st.position));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, target, bm_algo_q15_to_float(q15st.position));

    /* 趋势一致性：收敛所需步数量级相近（非逐步数值对照）。 */
    step_diff = f_steps - q31_steps;
    if (step_diff < 0) {
        step_diff = -step_diff;
    }
    TEST_ASSERT_TRUE(step_diff <= 20);
    step_diff = f_steps - q15_steps;
    if (step_diff < 0) {
        step_diff = -step_diff;
    }
    TEST_ASSERT_TRUE(step_diff <= 20);
}

/**
 * @brief 双轨对照：线性重采样 Q31/Q15 与 float 版逐步一致性
 *
 * 历史缺陷（已修复）：bm_algo_linear_resampler_reset() 初始相位
 * phase=1/ratio；ratio<1（降采样——Q15/Q31 的 ratio_q31/q15 定标域
 * 只能表达 (0,1)，因此 ratio<1 是唯一可用取值范围）时该初始相位恒
 * >1.0，且 step() 内部相位在多次调用间可能持续 >1.0（用于计数降
 * 采样跳步）。原桥接代码对相位字段直接复用通用
 * bm_algo_float_to_q31/q15()（±1.0 饱和裁剪），把真实初始相位错误
 * 拉回 [0,1) 定标域，导致 Q 版从第一次 step 起就比 float 版提前一次
 * 输出（曾用 ratio=0.999 复现：float 首步 n=0，Q31/Q15 首步 n=1）。
 * 现已改用相位字段专用、无 ±1.0 限幅的定标（见 bm_algo_fixed.c
 * resample_phase_q15/q31_from_float()，缩放系数分别为 1024 / 2^24），
 * 正确保留 >1.0 的相位语义。修复后逐步核验（ratio=0.999，15 步）：
 * f_cnt/q31_cnt/q15_cnt 每一步都严格相等（0,1,1,...,1），故本测试已
 * 升级为逐步对照，不再仅做趋势断言。
 */
static void test_dualtrack_linear_resampler_q_vs_float(void) {
    bm_algo_linear_resampler_state_t     fst;
    bm_algo_linear_resampler_q31_state_t q31st;
    bm_algo_linear_resampler_q15_state_t q15st;
    float                                f_out[8];
    bm_algo_q31_t                        q31_out[8];
    bm_algo_q15_t                        q15_out[8];
    uint32_t f_cnt, q31_cnt, q15_cnt;
    uint32_t f_total = 0u, q31_total = 0u, q15_total = 0u;
    const float ratio = 0.999f;
    int i;

    bm_algo_linear_resampler_reset(&fst, ratio, 0.0f);
    bm_algo_linear_resampler_q31_reset(&q31st, bm_algo_float_to_q31(ratio), 0);
    bm_algo_linear_resampler_q15_reset(&q15st, bm_algo_float_to_q15(ratio), 0);

    for (i = 0; i < 15; ++i) {
        float x = 0.3f * sinf((float)i * 0.2f);
        int f_rc, q31_rc, q15_rc;
        uint32_t j;

        f_rc = bm_algo_linear_resampler_step(&fst, x, f_out, 8u, &f_cnt);
        q31_rc = bm_algo_linear_resampler_q31_step(&q31st,
            bm_algo_float_to_q31(x), q31_out, 8u, &q31_cnt);
        q15_rc = bm_algo_linear_resampler_q15_step(&q15st,
            bm_algo_float_to_q15(x), q15_out, 8u, &q15_cnt);

        TEST_ASSERT_TRUE(f_rc >= 0);
        TEST_ASSERT_TRUE(q31_rc >= 0);
        TEST_ASSERT_TRUE(q15_rc >= 0);
        f_total += f_cnt;
        q31_total += q31_cnt;
        q15_total += q15_cnt;
        /* 逐步对照：修复后 Q31/Q15 每步输出计数与 float 版严格一致
         * （相位定标修复消除了原先的"提前一次输出"错位）。 */
        TEST_ASSERT_EQUAL_UINT32(f_cnt, q31_cnt);
        TEST_ASSERT_EQUAL_UINT32(f_cnt, q15_cnt);
        for (j = 0; j < q31_cnt; ++j) {
            float qv = bm_algo_q31_to_float(q31_out[j]);
            TEST_ASSERT_TRUE(qv >= -1.0f && qv <= 1.0f);
        }
        for (j = 0; j < q15_cnt; ++j) {
            float qv = bm_algo_q15_to_float(q15_out[j]);
            TEST_ASSERT_TRUE(qv >= -1.0f && qv <= 1.0f);
        }
    }

    /* 趋势一致性：三版本累计输出样本数量级相近（非逐步比对）。 */
    TEST_ASSERT_TRUE(f_total >= 10u);
    TEST_ASSERT_TRUE(q31_total >= 10u);
    TEST_ASSERT_TRUE(q15_total >= 10u);
}

void test_algo_fixed(void) {
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
    RUN_TEST(test_review_batch2_trapezoid_q31_vs_float);
    RUN_TEST(test_review_batch2_energy_scale_consistency);
    RUN_TEST(test_review_batch2_backlash_q31_vs_float);
    RUN_TEST(test_review_batch2_pid_q15_antiwindup_no_overflow);
    RUN_TEST(test_dualtrack_differentiator_q_vs_float);
    RUN_TEST(test_dualtrack_smith_predictor_q_vs_float);
    RUN_TEST(test_dualtrack_moving_avg_q_vs_float);
    RUN_TEST(test_dualtrack_complementary_q_vs_float);
    RUN_TEST(test_dualtrack_mahony_q_vs_float);
    RUN_TEST(test_dualtrack_madgwick_q_vs_float);
    RUN_TEST(test_dualtrack_dda_q_vs_float);
    RUN_TEST(test_dualtrack_sogi_pll_q_vs_float);
    RUN_TEST(test_dualtrack_rms_q_vs_float);
    RUN_TEST(test_dualtrack_flux_observer_q_vs_float);
    RUN_TEST(test_dualtrack_scurve_q_vs_float);
    RUN_TEST(test_dualtrack_linear_resampler_q_vs_float);
}

int main(void) {
    UNITY_BEGIN();
    test_algo_fixed();
    return UNITY_END();
}

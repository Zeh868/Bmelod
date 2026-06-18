/**
 * @file bm_algo_filter.c
 * @brief 滤波算法实现
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
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>
#include <string.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

int bm_algo_lpf1_init_from_cutoff(bm_algo_lpf1_config_t *config,
                                  float cutoff_hz,
                                  float sample_hz) {
    float rc;
    float dt;

    if (config == NULL || cutoff_hz <= 0.0f || sample_hz <= 0.0f) {
        return -1;
    }

    dt = 1.0f / sample_hz;
    rc = 1.0f / (2.0f * BM_ALGO_PI_F * cutoff_hz);
    config->alpha = dt / (rc + dt);
    return 0;
}

void bm_algo_lpf1_reset(bm_algo_lpf1_state_t *state, float value) {
    if (state != NULL) {
        state->output = value;
    }
}

float bm_algo_lpf1_step(bm_algo_lpf1_state_t *state,
                        const bm_algo_lpf1_config_t *config,
                        float input) {
    float alpha;

    if (state == NULL || config == NULL) {
        return input;
    }

    alpha = bm_algo_clamp_f(config->alpha, 0.0f, 1.0f);
    state->output += alpha * (input - state->output);
    return state->output;
}

int bm_algo_hpf1_init_from_cutoff(bm_algo_hpf1_config_t *config,
                                  float cutoff_hz,
                                  float sample_hz) {
    float rc;
    float dt;

    if (config == NULL || cutoff_hz <= 0.0f || sample_hz <= 0.0f) {
        return -1;
    }

    dt = 1.0f / sample_hz;
    rc = 1.0f / (2.0f * BM_ALGO_PI_F * cutoff_hz);
    config->alpha = rc / (rc + dt);
    return 0;
}

void bm_algo_hpf1_reset(bm_algo_hpf1_state_t *state) {
    if (state != NULL) {
        state->prev_input = 0.0f;
        state->prev_output = 0.0f;
    }
}

float bm_algo_hpf1_step(bm_algo_hpf1_state_t *state,
                        const bm_algo_hpf1_config_t *config,
                        float input) {
    float alpha;
    float out;

    if (state == NULL || config == NULL) {
        return input;
    }

    alpha = bm_algo_clamp_f(config->alpha, 0.0f, 1.0f);
    out = alpha * (state->prev_output + input - state->prev_input);
    state->prev_input = input;
    state->prev_output = out;
    return out;
}

int bm_algo_moving_avg_init(bm_algo_moving_avg_state_t *state,
                            const bm_algo_moving_avg_config_t *config) {
    if (state == NULL || config == NULL ||
        config->buffer == NULL || config->length == 0u) {
        return -1;
    }
    bm_algo_moving_avg_reset(state, config);
    return 0;
}

void bm_algo_moving_avg_reset(bm_algo_moving_avg_state_t *state,
                              const bm_algo_moving_avg_config_t *config) {
    if (state == NULL || config == NULL ||
        config->buffer == NULL || config->length == 0u) {
        return;
    }
    state->sum = 0.0f;
    state->index = 0u;
    state->count = 0u;
    state->length = config->length;
    memset(config->buffer, 0, config->length * sizeof(float));
}

float bm_algo_moving_avg_step(bm_algo_moving_avg_state_t *state,
                              const bm_algo_moving_avg_config_t *config,
                              float input) {
    float old;

    if (state == NULL || config == NULL ||
        config->buffer == NULL || config->length == 0u ||
        config->length != state->length ||
        state->index >= state->length) {
        return input;
    }

    old = config->buffer[state->index];
    config->buffer[state->index] = input;
    state->sum += input - old;

    state->index++;
    if (state->index >= config->length) {
        state->index = 0u;
    }
    if (state->count < config->length) {
        state->count++;
    }

    return state->sum / (float)state->count;
}

static void median_sort_small(float *a, uint32_t n) {
    uint32_t i;
    uint32_t j;
    float t;

    for (i = 1u; i < n; ++i) {
        for (j = i; j > 0u && a[j] < a[j - 1u]; --j) {
            t = a[j];
            a[j] = a[j - 1u];
            a[j - 1u] = t;
        }
    }
}

void bm_algo_median_reset(bm_algo_median_state_t *state) {
    if (state != NULL) {
        state->index = 0u;
        state->count = 0u;
    }
}

float bm_algo_median_step(bm_algo_median_state_t *state,
                          const bm_algo_median_config_t *config,
                          float input) {
    float sorted[BM_ALGO_MEDIAN_MAX_TAPS];
    uint32_t len;
    uint32_t i;

    if (state == NULL || config == NULL) {
        return input;
    }

    len = config->length;
    if (len < 3u || len > BM_ALGO_MEDIAN_MAX_TAPS || (len & 1u) == 0u) {
        return input;
    }

    state->buffer[state->index] = input;
    state->index = (state->index + 1u) % len;
    if (state->count < len) {
        state->count++;
    }

    for (i = 0u; i < state->count; ++i) {
        sorted[i] = state->buffer[i];
    }
    median_sort_small(sorted, state->count);
    return sorted[state->count / 2u];
}

int bm_algo_fir_init(bm_algo_fir_state_t *state,
                     const bm_algo_fir_config_t *config) {
    if (state == NULL || config == NULL ||
        config->coeffs == NULL || config->delay_line == NULL ||
        config->tap_count == 0u) {
        return -1;
    }
    bm_algo_fir_reset(state, config);
    return 0;
}

void bm_algo_fir_reset(bm_algo_fir_state_t *state,
                       const bm_algo_fir_config_t *config) {
    if (state == NULL || config == NULL || config->delay_line == NULL) {
        return;
    }
    state->index = 0u;
    state->tap_count = config->tap_count;
    memset(config->delay_line, 0, config->tap_count * sizeof(float));
}

float bm_algo_fir_step(bm_algo_fir_state_t *state,
                       const bm_algo_fir_config_t *config,
                       float input) {
    float acc = 0.0f;
    uint32_t i;
    uint32_t idx;

    if (state == NULL || config == NULL ||
        config->coeffs == NULL || config->delay_line == NULL ||
        config->tap_count == 0u ||
        config->tap_count != state->tap_count ||
        state->index >= state->tap_count) {
        return input;
    }

    config->delay_line[state->index] = input;

    for (i = 0u; i < config->tap_count; ++i) {
        idx = (state->index + config->tap_count - i) % config->tap_count;
        acc += config->coeffs[i] * config->delay_line[idx];
    }

    state->index = (state->index + 1u) % config->tap_count;
    return acc;
}

int bm_algo_biquad_design(bm_algo_biquad_config_t *config,
                          const bm_algo_biquad_design_t *design) {
    float w0;
    float cos_w0;
    float sin_w0;
    float alpha;
    float A;
    float b0;
    float b1;
    float b2;
    float a0;
    float a1;
    float a2;

    if (config == NULL || design == NULL ||
        design->sample_hz <= 0.0f || design->freq_hz <= 0.0f ||
        design->freq_hz >= 0.5f * design->sample_hz ||
        design->q <= 0.0f ||
        !bm_algo_is_finite_f(design->sample_hz) ||
        !bm_algo_is_finite_f(design->freq_hz) ||
        !bm_algo_is_finite_f(design->q) ||
        !bm_algo_is_finite_f(design->gain_db)) {
        return -1;
    }

    w0 = 2.0f * BM_ALGO_PI_F * design->freq_hz / design->sample_hz;
    cos_w0 = cosf(w0);
    sin_w0 = sinf(w0);
    alpha = sin_w0 / (2.0f * design->q);
    A = powf(10.0f, design->gain_db / 40.0f);

    switch (design->type) {
    case BM_ALGO_BIQUAD_LPF:
        b0 = (1.0f - cos_w0) / 2.0f;
        b1 = 1.0f - cos_w0;
        b2 = (1.0f - cos_w0) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha;
        break;
    case BM_ALGO_BIQUAD_HPF:
        b0 = (1.0f + cos_w0) / 2.0f;
        b1 = -(1.0f + cos_w0);
        b2 = (1.0f + cos_w0) / 2.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha;
        break;
    case BM_ALGO_BIQUAD_NOTCH:
        b0 = 1.0f;
        b1 = -2.0f * cos_w0;
        b2 = 1.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha;
        break;
    case BM_ALGO_BIQUAD_BPF:
        b0 = alpha * A;
        b1 = 0.0f;
        b2 = -alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha / A;
        break;
    case BM_ALGO_BIQUAD_PEAKING:
        b0 = 1.0f + alpha * A;
        b1 = -2.0f * cos_w0;
        b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = -2.0f * cos_w0;
        a2 = 1.0f - alpha / A;
        break;
    default:
        return -1;
    }

    config->b0 = b0 / a0;
    config->b1 = b1 / a0;
    config->b2 = b2 / a0;
    config->a1 = a1 / a0;
    config->a2 = a2 / a0;
    return 0;
}

void bm_algo_biquad_reset(bm_algo_biquad_state_t *state) {
    if (state != NULL) {
        state->z1 = 0.0f;
        state->z2 = 0.0f;
    }
}

float bm_algo_biquad_step(bm_algo_biquad_state_t *state,
                          const bm_algo_biquad_config_t *config,
                          float input) {
    float output;

    if (state == NULL || config == NULL) {
        return input;
    }

    /* 直接 II 型转置 */
    output = config->b0 * input + state->z1;
    state->z1 = config->b1 * input - config->a1 * output + state->z2;
    state->z2 = config->b2 * input - config->a2 * output;
    return output;
}

int bm_algo_biquad_notch_update(bm_algo_biquad_config_t *config,
                                bm_algo_biquad_state_t *state,
                                float sample_hz,
                                float center_hz,
                                float q) {
    bm_algo_biquad_design_t design;

    if (config == NULL) {
        return -1;
    }

    design.type = BM_ALGO_BIQUAD_NOTCH;
    design.sample_hz = sample_hz;
    design.freq_hz = center_hz;
    design.q = q;
    design.gain_db = 0.0f;

    if (bm_algo_biquad_design(config, &design) != 0) {
        return -1;
    }

    if (state != NULL) {
        /* 保留 z 状态，仅更新系数 */
    }
    return 0;
}

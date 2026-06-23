/**
 * @file bm_algo_resample.c
 * @brief 重采样实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加多相 FIR 抽取
 * 2026-06-23       1.1            zeh            补齐 Doxygen 注释
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_resample.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <limits.h>
#include <math.h>
#include <string.h>

void bm_algo_decimator_reset(bm_algo_decimator_state_t *state) {
    if (state != NULL) {
        state->counter = 0u;
        state->decim = 1u;
    }
}

int bm_algo_decimator_step(bm_algo_decimator_state_t *state,
                           uint32_t decim,
                           float input,
                           float *output) {
    if (state == NULL || decim == 0u) {
        return 0;
    }

    state->decim = decim;
    if (state->counter == 0u) {
        if (output != NULL) {
            *output = input;
        }
        state->counter = decim - 1u;
        return 1;
    }

    if (state->counter > 0u) {
        state->counter--;
    }
    return 0;
}

void bm_algo_linear_resampler_reset(bm_algo_linear_resampler_state_t *state,
                                    float ratio,
                                    float initial) {
    if (state != NULL) {
        state->ratio = (ratio > 0.0f && bm_algo_is_finite_f(ratio))
                           ? ratio
                           : 0.0f;
        state->phase = (state->ratio > 0.0f)
                           ? (1.0f / state->ratio)
                           : 0.0f;
        state->prev_sample = initial;
    }
}

int bm_algo_linear_resampler_step(bm_algo_linear_resampler_state_t *state,
                                  float input,
                                  float *outputs,
                                  uint32_t max_outputs,
                                  uint32_t *out_count) {
    uint32_t n = 0u;
    uint64_t required = 0u;
    float phase;
    float step;

    if (state == NULL || outputs == NULL || out_count == NULL ||
        max_outputs == 0u || state->ratio <= 0.0f) {
        if (out_count != NULL) {
            *out_count = 0u;
        }
        return 0;
    }

    step = 1.0f / state->ratio;
    phase = state->phase;
    if (!bm_algo_is_finite_f(step) || !bm_algo_is_finite_f(phase) ||
        step <= 0.0f) {
        *out_count = 0u;
        return -1;
    }
    while (phase <= 1.0f && required <= (uint64_t)max_outputs) {
        required++;
        phase += step;
    }
    if (required > (uint64_t)max_outputs || required > (uint64_t)INT_MAX) {
        *out_count = 0u;
        return -1;
    }

    phase = state->phase;
    while (phase <= 1.0f) {
        outputs[n++] = state->prev_sample * (1.0f - phase)
                       + input * phase;
        phase += step;
    }

    state->phase = phase - 1.0f;
    state->prev_sample = input;
    *out_count = n;
    return (int)n;
}

int bm_algo_polyphase_decim_init(bm_algo_polyphase_decim_state_t *state,
                                 const bm_algo_polyphase_decim_config_t *config,
                                 float *delay_line,
                                 uint32_t delay_len) {
    if (state == NULL || config == NULL || delay_line == NULL ||
        config->coeffs == NULL || config->tap_count == 0u ||
        config->decim == 0u || delay_len < config->tap_count) {
        return -1;
    }

    state->delay_line = delay_line;
    state->delay_len = delay_len;
    state->tap_count = config->tap_count;
    state->decim = config->decim;
    bm_algo_polyphase_decim_reset(state, config);
    return 0;
}

void bm_algo_polyphase_decim_reset(bm_algo_polyphase_decim_state_t *state,
                                   const bm_algo_polyphase_decim_config_t *config) {
    if (state == NULL || state->delay_line == NULL) {
        return;
    }

    state->index = 0u;
    state->decim_counter = 0u;
    memset(state->delay_line, 0, state->delay_len * sizeof(float));
    (void)config;
}

uint32_t bm_algo_polyphase_decim_process(bm_algo_polyphase_decim_state_t *state,
                                         const bm_algo_polyphase_decim_config_t *config,
                                         const float *in,
                                         float *out,
                                         uint32_t in_count) {
    uint32_t out_count = 0u;
    uint32_t i;
    uint32_t k;
    uint32_t idx;

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        state->delay_line == NULL || config->coeffs == NULL ||
        config->tap_count == 0u || config->tap_count > state->delay_len ||
        config->tap_count != state->tap_count ||
        config->decim != state->decim ||
        config->decim == 0u || in_count == 0u ||
        state->index >= config->tap_count) {
        return 0u;
    }

    for (i = 0u; i < in_count; ++i) {
        state->delay_line[state->index] = in[i];
        state->index = (state->index + 1u) % config->tap_count;

        if (state->decim_counter == 0u) {
            float acc = 0.0f;

            for (k = 0u; k < config->tap_count; ++k) {
                idx = (state->index + config->tap_count - 1u - k) %
                      config->tap_count;
                acc += config->coeffs[k] * state->delay_line[idx];
            }
            out[out_count++] = acc;
            state->decim_counter = config->decim - 1u;
        } else {
            state->decim_counter--;
        }
    }

    return out_count;
}

void bm_algo_clock_drift_reset(bm_algo_clock_drift_state_t *state) {
    if (state != NULL) {
        state->ratio_error = 0.0f;
    }
}

void bm_algo_clock_drift_feed(bm_algo_clock_drift_state_t *state,
                              const bm_algo_clock_drift_config_t *config,
                              float expected_dt_s,
                              float actual_dt_s) {
    float measured;
    float alpha;

    if (state == NULL || config == NULL ||
        expected_dt_s <= 0.0f || actual_dt_s <= 0.0f) {
        return;
    }

    measured = actual_dt_s / expected_dt_s - 1.0f;
    alpha = config->alpha;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    state->ratio_error = alpha * measured +
                         (1.0f - alpha) * state->ratio_error;
}

float bm_algo_clock_drift_compensate(const bm_algo_clock_drift_state_t *state,
                                     float actual_dt_s) {
    float denom;

    if (state == NULL || actual_dt_s <= 0.0f) {
        return actual_dt_s;
    }

    denom = 1.0f + state->ratio_error;
    if (denom <= 1e-6f) {
        return actual_dt_s;
    }
    return actual_dt_s / denom;
}

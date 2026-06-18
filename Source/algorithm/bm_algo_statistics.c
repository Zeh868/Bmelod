/**
 * @file bm_algo_statistics.c
 * @brief 统计量实现
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
#include "bm/algorithm/bm_algo_statistics.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

void bm_algo_stats_reset(bm_algo_stats_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->sum = 0.0f;
    state->sum_sq = 0.0f;
    state->mean = 0.0f;
    state->m2 = 0.0f;
    state->min_v = 0.0f;
    state->max_v = 0.0f;
    state->count = 0u;
}

void bm_algo_stats_push(bm_algo_stats_state_t *state, float value) {
    float delta;
    float delta2;

    if (state == NULL) {
        return;
    }
    if (state->count == UINT32_MAX || !bm_algo_is_finite_f(value)) {
        return;
    }

    if (state->count == 0u) {
        state->min_v = value;
        state->max_v = value;
    } else {
        if (value < state->min_v) {
            state->min_v = value;
        }
        if (value > state->max_v) {
            state->max_v = value;
        }
    }

    state->sum += value;
    state->sum_sq += value * value;
    state->count++;
    delta = value - state->mean;
    state->mean += delta / (float)state->count;
    delta2 = value - state->mean;
    state->m2 += delta * delta2;
}

float bm_algo_stats_mean(const bm_algo_stats_state_t *state) {
    if (state == NULL || state->count == 0u) {
        return 0.0f;
    }
    return state->mean;
}

float bm_algo_stats_variance(const bm_algo_stats_state_t *state) {
    if (state == NULL || state->count < 2u) {
        return 0.0f;
    }

    return state->m2 / (float)state->count;
}

float bm_algo_stats_rms(const bm_algo_stats_state_t *state) {
    if (state == NULL || state->count == 0u) {
        return 0.0f;
    }
    return sqrtf(state->sum_sq / (float)state->count);
}

float bm_algo_stats_crest_factor(const bm_algo_stats_state_t *state) {
    float rms;
    float peak;

    if (state == NULL || state->count == 0u) {
        return 0.0f;
    }

    rms = bm_algo_stats_rms(state);
    peak = fmaxf(fabsf(state->min_v), fabsf(state->max_v));
    if (rms < 1e-12f) {
        return 0.0f;
    }
    return peak / rms;
}

float bm_algo_array_mean(const float *data, uint32_t n) {
    float sum = 0.0f;
    uint32_t i;

    if (data == NULL || n == 0u) {
        return 0.0f;
    }

    for (i = 0u; i < n; ++i) {
        sum += data[i];
    }
    return sum / (float)n;
}

float bm_algo_array_rms(const float *data, uint32_t n) {
    float sum_sq = 0.0f;
    uint32_t i;

    if (data == NULL || n == 0u) {
        return 0.0f;
    }

    for (i = 0u; i < n; ++i) {
        sum_sq += data[i] * data[i];
    }
    return sqrtf(sum_sq / (float)n);
}

float bm_algo_array_peak(const float *data, uint32_t n) {
    float peak = 0.0f;
    uint32_t i;

    if (data == NULL || n == 0u) {
        return 0.0f;
    }

    for (i = 0u; i < n; ++i) {
        float a = fabsf(data[i]);
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

void bm_algo_rate_est_reset(bm_algo_rate_est_state_t *state, float input) {
    if (state == NULL) {
        return;
    }
    state->prev_input = input;
    state->rate_per_s = 0.0f;
}

float bm_algo_rate_est_step(bm_algo_rate_est_state_t *state,
                            float input,
                            float dt_s) {
    if (state == NULL || dt_s <= 0.0f) {
        return state != NULL ? state->rate_per_s : 0.0f;
    }

    state->rate_per_s = (input - state->prev_input) / dt_s;
    state->prev_input = input;
    return state->rate_per_s;
}

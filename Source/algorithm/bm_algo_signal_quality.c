/**
 * @file bm_algo_signal_quality.c
 * @brief 信号质量监控实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            stable_count 自增前加饱和保护，防止 uint32_t 绕回导致误复位
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_signal_quality.h"
#include <stddef.h>
#include <stdint.h>

#include <math.h>

void bm_algo_debounce_analog_reset(bm_algo_debounce_analog_state_t *state,
                                   float initial) {
    if (state != NULL) {
        state->candidate = initial;
        state->stable_count = 0u;
        state->latched = initial;
        state->valid = 1;
    }
}

int bm_algo_debounce_analog_step(bm_algo_debounce_analog_state_t *state,
                                 const bm_algo_debounce_analog_config_t *config,
                                 float sample) {
    float diff;

    if (state == NULL || config == NULL) {
        return 0;
    }

    diff = fabsf(sample - state->candidate);
    if (diff <= config->tolerance) {
        /* 饱和加法，防止 uint32_t 绕回 */
        if (state->stable_count < UINT32_MAX) {
            state->stable_count++;
        }
    } else {
        state->candidate = sample;
        state->stable_count = 0u;
    }

    if (state->stable_count >= config->stable_count_required) {
        state->latched = state->candidate;
        state->valid = 1;
        return 1;
    }

    return 0;
}

void bm_algo_range_monitor_reset(bm_algo_range_monitor_state_t *state, float v) {
    if (state != NULL) {
        state->prev = v;
        state->fault_flags = 0u;
    }
}

uint32_t bm_algo_range_monitor_step(bm_algo_range_monitor_state_t *state,
                                    const bm_algo_range_monitor_config_t *config,
                                    float sample,
                                    float dt_s) {
    float rate;
    uint32_t flags = 0u;

    if (state == NULL || config == NULL) {
        return 0u;
    }

    if (sample < config->min_v) {
        flags |= BM_ALGO_FAULT_UNDER_RANGE;
    }
    if (sample > config->max_v) {
        flags |= BM_ALGO_FAULT_OVER_RANGE;
    }

    if (dt_s > 0.0f) {
        rate = fabsf(sample - state->prev) / dt_s;
        if (rate > config->max_rate_per_s) {
            flags |= BM_ALGO_FAULT_RATE;
        }
    }

    if (sample == state->prev) {
        flags |= BM_ALGO_FAULT_FROZEN;
    }

    state->prev = sample;
    state->fault_flags = flags;
    return flags;
}

uint32_t bm_algo_redundant_pair_step(float a,
                                     float b,
                                     const bm_algo_redundant_pair_config_t *config) {
    float diff;
    float tol;
    float ref;

    diff = fabsf(a - b);
    if (config == NULL) {
        return (diff > 0.0f) ? BM_ALGO_FAULT_REDUNDANT_MISMATCH : 0u;
    }

    ref = fabsf(a);
    if (fabsf(b) > ref) {
        ref = fabsf(b);
    }
    tol = config->tolerance_abs + config->tolerance_rel * ref;
    if (diff > tol) {
        return BM_ALGO_FAULT_REDUNDANT_MISMATCH;
    }
    return 0u;
}

/**
 * @file bm_algo_common.c
 * @brief 算法公共工具实现
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
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

float bm_algo_clamp_f(float value, float min_v, float max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

float bm_algo_saturate_f(float value, float limit) {
    return bm_algo_clamp_f(value, -limit, limit);
}

float bm_algo_deadband_f(float value, float width) {
    if (width <= 0.0f) {
        return value;
    }
    if (value > width) {
        return value - width;
    }
    if (value < -width) {
        return value + width;
    }
    return 0.0f;
}

void bm_algo_hysteresis_reset(bm_algo_hysteresis_state_t *state, int output_high) {
    if (state != NULL) {
        state->latch = output_high;
    }
}

float bm_algo_hysteresis_step(bm_algo_hysteresis_state_t *state,
                              const bm_algo_hysteresis_config_t *config,
                              float value) {
    if (state == NULL || config == NULL) {
        return value;
    }
    if (state->latch) {
        if (value < config->low_threshold) {
            state->latch = 0;
        }
    } else {
        if (value > config->high_threshold) {
            state->latch = 1;
        }
    }
    return state->latch ? 1.0f : 0.0f;
}

void bm_algo_rate_limit_reset(bm_algo_rate_limit_state_t *state, float output) {
    if (state != NULL) {
        state->output = output;
    }
}

float bm_algo_rate_limit_step(bm_algo_rate_limit_state_t *state,
                              const bm_algo_rate_limit_config_t *config,
                              float target,
                              float dt_s) {
    float delta;
    float max_up;
    float max_down;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return target;
    }

    delta = target - state->output;
    max_up = config->max_rise_per_s * dt_s;
    max_down = config->max_fall_per_s * dt_s;

    if (delta > max_up) {
        delta = max_up;
    } else if (delta < -max_down) {
        delta = -max_down;
    }

    state->output += delta;
    return state->output;
}

float bm_algo_angle_wrap_rad(float angle_rad) {
    const float two_pi = 2.0f * BM_ALGO_PI_F;

    if (!bm_algo_is_finite_f(angle_rad)) {
        return angle_rad;
    }
    angle_rad = fmodf(angle_rad + BM_ALGO_PI_F, two_pi);
    if (angle_rad < 0.0f) {
        angle_rad += two_pi;
    }
    return angle_rad - BM_ALGO_PI_F;
}

float bm_algo_angle_wrap_0_2pi_rad(float angle_rad) {
    const float two_pi = 2.0f * BM_ALGO_PI_F;

    if (!bm_algo_is_finite_f(angle_rad)) {
        return angle_rad;
    }
    angle_rad = fmodf(angle_rad, two_pi);
    if (angle_rad < 0.0f) {
        angle_rad += two_pi;
    }
    return angle_rad;
}

float bm_algo_angle_delta_rad(float from_rad, float to_rad) {
    return bm_algo_angle_wrap_rad(to_rad - from_rad);
}

int bm_algo_is_finite_f(float value) {
#if defined(isfinite)
    return isfinite(value) != 0;
#else
    return (value == value) && (value * 0.0f == 0.0f);
#endif
}

/**
 * @file bm_algo_compensation.c
 * @brief 执行器非线性补偿实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_compensation.h"
#include <stddef.h>

#include <math.h>

float bm_algo_deadzone_inverse(float command, float deadband, float gain) {
    float a;

    if (deadband <= 0.0f || gain <= 0.0f) {
        return command;
    }

    a = fabsf(command);
    if (a <= deadband) {
        return 0.0f;
    }
    if (command > 0.0f) {
        return gain * (command - deadband);
    }
    return gain * (command + deadband);
}

float bm_algo_friction_comp(float velocity,
                            float coulomb,
                            float viscous,
                            float v_deadband) {
    float sign_v;

    if (fabsf(velocity) < v_deadband) {
        return 0.0f;
    }
    sign_v = (velocity > 0.0f) ? 1.0f : -1.0f;
    return sign_v * coulomb + viscous * velocity;
}

void bm_algo_dob_reset(bm_algo_dob_state_t *state) {
    if (state != NULL) {
        state->y_hat = 0.0f;
        state->disturbance = 0.0f;
    }
}

float bm_algo_dob_step(bm_algo_dob_state_t *state,
                       const bm_algo_dob_config_t *config,
                       float u,
                       float y,
                       float *disturbance_out) {
    float alpha;
    float residual;

    if (state == NULL || config == NULL) {
        if (disturbance_out != NULL) {
            *disturbance_out = 0.0f;
        }
        return 0.0f;
    }

    state->y_hat = config->plant_gain * u;
    residual = y - state->y_hat;
    alpha = config->lpf_alpha;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    state->disturbance = alpha * residual +
                         (1.0f - alpha) * state->disturbance;

    if (disturbance_out != NULL) {
        *disturbance_out = state->disturbance;
    }
    return state->disturbance;
}

void bm_algo_backlash_reset(bm_algo_backlash_state_t *state) {
    if (state != NULL) {
        state->last_direction = 0;
        state->backlash_offset = 0.0f;
    }
}

float bm_algo_backlash_inverse(float command,
                               bm_algo_backlash_state_t *state,
                               float width,
                               float slope) {
    int direction;
    float out;

    if (state == NULL || width <= 0.0f || slope <= 0.0f) {
        return command;
    }

    if (command > 0.0f) {
        direction = 1;
    } else if (command < 0.0f) {
        direction = -1;
    } else {
        direction = state->last_direction;
    }

    if (direction != 0 && direction != state->last_direction &&
        state->last_direction != 0) {
        if (state->backlash_offset < width) {
            state->backlash_offset += slope;
            if (state->backlash_offset > width) {
                state->backlash_offset = width;
            }
        }
    }

    if (direction != 0) {
        state->last_direction = direction;
    }

    out = command;
    if (direction > 0) {
        out += state->backlash_offset;
    } else if (direction < 0) {
        out -= state->backlash_offset;
    }
    return out;
}

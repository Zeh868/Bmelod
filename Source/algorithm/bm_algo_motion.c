/**
 * @file bm_algo_motion.c
 * @brief 运动辅助：编码器与 DDA 实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加步进脉冲生成
 * 2026-06-23       1.1            zeh            补齐 Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_motion.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <limits.h>
#include <math.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

void bm_algo_encoder_reset(bm_algo_encoder_state_t *state,
                           const bm_algo_encoder_config_t *config,
                           int32_t raw_count) {
    float counts_to_rad;

    if (state == NULL || config == NULL) {
        return;
    }
    state->prev_count = raw_count;
    state->turns = 0;
    counts_to_rad = (config->counts_per_rev > 0u)
                        ? (2.0f * BM_ALGO_PI_F /
                           (float)config->counts_per_rev)
                        : 0.0f;
    state->position_rad = (float)raw_count * counts_to_rad;
    state->velocity_rad_s = 0.0f;
}

float bm_algo_encoder_update(bm_algo_encoder_state_t *state,
                             const bm_algo_encoder_config_t *config,
                             int32_t raw_count,
                             float dt_s) {
    int64_t delta;
    float counts_to_rad;
    float pos_counts;

    if (state == NULL || config == NULL || config->counts_per_rev == 0u ||
        dt_s <= 0.0f) {
        return 0.0f;
    }

    delta = (int64_t)raw_count - (int64_t)state->prev_count;
    if (delta > (int64_t)(config->counts_per_rev / 2u)) {
        if (state->turns > INT32_MIN) {
            state->turns--;
        }
    } else if (delta < -(int64_t)(config->counts_per_rev / 2u)) {
        if (state->turns < INT32_MAX) {
            state->turns++;
        }
    }

    state->prev_count = raw_count;
    pos_counts = (float)state->turns * (float)config->counts_per_rev
                 + (float)raw_count;
    counts_to_rad = 2.0f * BM_ALGO_PI_F / (float)config->counts_per_rev;

    state->velocity_rad_s =
        (pos_counts * counts_to_rad - state->position_rad) / dt_s;
    state->position_rad = pos_counts * counts_to_rad;
    return state->position_rad;
}

void bm_algo_dda_reset(bm_algo_dda_state_t *state,
                       const bm_algo_dda_config_t *config) {
    double dx;
    double dy;
    double distance;
    double required_steps;

    if (state == NULL || config == NULL) {
        return;
    }

    state->x = config->x0;
    state->y = config->y0;
    state->dx = 0.0f;
    state->dy = 0.0f;
    state->target_x = config->x1;
    state->target_y = config->y1;
    state->step_size = config->step_size;
    state->steps = 0u;
    state->step_count = 0u;
    state->err = 0.0f;
    state->step_x = 1;
    state->step_y = 1;
    state->done = 1;

    if (!bm_algo_is_finite_f(config->x0) ||
        !bm_algo_is_finite_f(config->y0) ||
        !bm_algo_is_finite_f(config->x1) ||
        !bm_algo_is_finite_f(config->y1) ||
        !bm_algo_is_finite_f(config->step_size) ||
        config->step_size <= 0.0f) {
        return;
    }

    dx = (double)config->x1 - (double)config->x0;
    dy = (double)config->y1 - (double)config->y0;
    distance = hypot(dx, dy);
    if (!isfinite(distance) || distance < 1e-6) {
        return;
    }
    required_steps = ceil(distance / (double)config->step_size);
    if (!isfinite(required_steps) || required_steps < 1.0 ||
        required_steps > (double)UINT32_MAX) {
        return;
    }

    state->dx = (float)dx;
    state->dy = (float)dy;
    state->steps = (uint32_t)required_steps;
    state->step_x = (state->dx >= 0.0f) ? 1 : -1;
    state->step_y = (state->dy >= 0.0f) ? 1 : -1;
    state->done = 0;
}

int bm_algo_dda_step(bm_algo_dda_state_t *state,
                     const bm_algo_dda_config_t *config,
                     float *x_out,
                     float *y_out) {
    float inc_x;
    float inc_y;

    if (state == NULL || config == NULL || state->done ||
        !bm_algo_is_finite_f(config->step_size) ||
        config->step_size <= 0.0f ||
        config->x1 != state->target_x ||
        config->y1 != state->target_y ||
        config->step_size != state->step_size) {
        return 0;
    }

    if (state->steps == 0u) {
        state->done = 1;
        return 0;
    }

    inc_x = state->dx / (float)state->steps;
    inc_y = state->dy / (float)state->steps;
    state->x += inc_x;
    state->y += inc_y;
    state->step_count++;

    if (state->step_count >= state->steps) {
        state->x = state->target_x;
        state->y = state->target_y;
        state->done = 1;
    }

    if (x_out != NULL) {
        *x_out = state->x;
    }
    if (y_out != NULL) {
        *y_out = state->y;
    }
    return 1;
}

void bm_algo_stepper_reset(bm_algo_stepper_state_t *state, int32_t position) {
    if (state != NULL) {
        state->phase = 0.0f;
        state->position_steps = position;
    }
}

uint32_t bm_algo_stepper_process(bm_algo_stepper_state_t *state,
                                 const bm_algo_stepper_config_t *config,
                                 float velocity_steps_s,
                                 float dt_s,
                                 int8_t *pulses,
                                 uint32_t max_pulses) {
    float max_vel;
    int8_t dir;
    uint32_t count = 0u;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return 0u;
    }

    max_vel = config->max_velocity_steps_s;
    if (max_vel > 0.0f) {
        if (velocity_steps_s > max_vel) {
            velocity_steps_s = max_vel;
        } else if (velocity_steps_s < -max_vel) {
            velocity_steps_s = -max_vel;
        }
    }

    if (velocity_steps_s == 0.0f) {
        return 0u;
    }

    dir = (velocity_steps_s > 0.0f) ? 1 : -1;
    state->phase += fabsf(velocity_steps_s) * dt_s;

    while (state->phase >= 1.0f) {
        if (pulses != NULL && count >= max_pulses) {
            break;
        }
        state->phase -= 1.0f;
        state->position_steps += dir;
        if (pulses != NULL) {
            pulses[count++] = dir;
        } else {
            count++;
        }
    }

    return count;
}

void bm_algo_encoder_diag_reset(bm_algo_encoder_diag_state_t *state,
                                int32_t raw_count) {
    if (state != NULL) {
        state->prev_count = raw_count;
    }
}

uint32_t bm_algo_encoder_diag_step(bm_algo_encoder_diag_state_t *state,
                                   const bm_algo_encoder_diag_config_t *config,
                                   int32_t raw_count,
                                   int index_pulse_seen) {
    uint32_t faults = BM_ALGO_ENCODER_FAULT_NONE;
    int32_t delta;

    if (state == NULL || config == NULL) {
        return faults;
    }

    delta = raw_count - state->prev_count;
    if (config->max_delta_per_step > 0 &&
        (delta > config->max_delta_per_step ||
         delta < -config->max_delta_per_step)) {
        faults |= BM_ALGO_ENCODER_FAULT_MISSED;
    }

    if (index_pulse_seen != 0) {
        faults |= BM_ALGO_ENCODER_FAULT_INDEX;
    }

    state->prev_count = raw_count;
    return faults;
}

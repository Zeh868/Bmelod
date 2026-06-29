/**
 * @file bm_algo_profile.c
 * @brief 轨迹规划：斜坡、梯形与 S 曲线实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.0            zeh            补齐 Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

void bm_algo_ramp_reset(bm_algo_ramp_state_t *state, float output) {
    if (state != NULL) {
        state->output = output;
        state->done = 1;
    }
}

float bm_algo_ramp_step(bm_algo_ramp_state_t *state,
                        const bm_algo_ramp_config_t *config,
                        float target,
                        float dt_s) {
    float delta;
    float step;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return target;
    }

    delta = target - state->output;
    step = config->rate_per_s * dt_s;

    if (fabsf(delta) <= step) {
        state->output = target;
        state->done = 1;
    } else {
        state->output += (delta > 0.0f) ? step : -step;
        state->done = 0;
    }
    return state->output;
}

void bm_algo_trapezoid_reset(bm_algo_trapezoid_state_t *state,
                             float position,
                             float velocity) {
    if (state != NULL) {
        state->position = position;
        state->velocity = velocity;
        state->target = position;
        state->phase = 3;
        state->done = 1;
    }
}

void bm_algo_trapezoid_set_target(bm_algo_trapezoid_state_t *state, float target) {
    if (state != NULL) {
        state->target = target;
        state->done = 0;
        state->phase = 0;
    }
}

float bm_algo_trapezoid_step(bm_algo_trapezoid_state_t *state,
                             const bm_algo_trapezoid_config_t *config,
                             float dt_s) {
    float dist;
    float stop_dist;
    float accel;
    float decel;
    float previous_dist;

    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        config->max_vel <= 0.0f || config->max_accel <= 0.0f ||
        config->max_decel <= 0.0f) {
        return 0.0f;
    }

    dist = state->target - state->position;
    accel = config->max_accel;
    decel = config->max_decel;

    if (fabsf(dist) < 1e-6f && fabsf(state->velocity) < 1e-6f) {
        state->velocity = 0.0f;
        state->done = 1;
        return state->position;
    }

    stop_dist = (state->velocity * state->velocity) / (2.0f * decel);

    if (dist >= 0.0f) {
        if (dist > stop_dist || state->velocity < 0.0f) {
            state->velocity += accel * dt_s;
            if (state->velocity > config->max_vel) {
                state->velocity = config->max_vel;
            }
        } else {
            state->velocity -= decel * dt_s;
            if (state->velocity < 0.0f) {
                state->velocity = 0.0f;
            }
        }
    } else {
        if (-dist > stop_dist || state->velocity > 0.0f) {
            state->velocity -= accel * dt_s;
            if (state->velocity < -config->max_vel) {
                state->velocity = -config->max_vel;
            }
        } else {
            state->velocity += decel * dt_s;
            if (state->velocity > 0.0f) {
                state->velocity = 0.0f;
            }
        }
    }

    previous_dist = dist;
    state->position += state->velocity * dt_s;
    dist = state->target - state->position;
    if ((previous_dist > 0.0f && dist <= 0.0f) ||
        (previous_dist < 0.0f && dist >= 0.0f)) {
        state->position = state->target;
        state->velocity = 0.0f;
        state->done = 1;
        state->phase = 3;
    } else {
        state->done = 0;
    }
    return state->position;
}

void bm_algo_scurve_reset(bm_algo_scurve_state_t *state,
                          float position,
                          float velocity,
                          float acceleration) {
    if (state != NULL) {
        state->position = position;
        state->velocity = velocity;
        state->acceleration = acceleration;
        state->target = position;
        state->done = 1;
    }
}

void bm_algo_scurve_set_target(bm_algo_scurve_state_t *state, float target) {
    if (state != NULL) {
        state->target = target;
        state->done = 0;
    }
}

float bm_algo_scurve_step(bm_algo_scurve_state_t *state,
                          const bm_algo_scurve_config_t *config,
                          float dt_s) {
    float dist;
    float direction;
    float stopping_distance;
    float target_acceleration;
    float acceleration_delta;
    float previous_dist;

    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        config->max_vel <= 0.0f || config->max_accel <= 0.0f ||
        config->max_jerk <= 0.0f) {
        return 0.0f;
    }

    dist = state->target - state->position;
    if (fabsf(dist) < 1e-5f && fabsf(state->velocity) < 1e-5f) {
        state->position = state->target;
        state->velocity = 0.0f;
        state->acceleration = 0.0f;
        state->done = 1;
        return state->position;
    }

    /* 制动距离控制器：根据预估停车距离决策加速/减速，acceleration 变化量受 jerk 限制。 */
    direction = (dist >= 0.0f) ? 1.0f : -1.0f;
    stopping_distance =
        (state->velocity * state->velocity) / (2.0f * config->max_accel);
    stopping_distance +=
        fabsf(state->velocity * state->acceleration) / config->max_jerk;

    if (state->velocity * direction < 0.0f ||
        fabsf(dist) > stopping_distance) {
        target_acceleration =
            (fabsf(state->velocity) < config->max_vel)
                ? direction * config->max_accel
                : 0.0f;
    } else {
        target_acceleration = -direction * config->max_accel;
    }

    acceleration_delta = target_acceleration - state->acceleration;
    acceleration_delta = bm_algo_clamp_f(
        acceleration_delta,
        -config->max_jerk * dt_s,
        config->max_jerk * dt_s);
    state->acceleration += acceleration_delta;
    state->acceleration = bm_algo_clamp_f(state->acceleration,
                                          -config->max_accel,
                                          config->max_accel);

    state->velocity += state->acceleration * dt_s;
    state->velocity = bm_algo_clamp_f(state->velocity,
                                      -config->max_vel,
                                      config->max_vel);

    previous_dist = dist;
    state->position += state->velocity * dt_s;
    dist = state->target - state->position;
    if ((previous_dist > 0.0f && dist <= 0.0f) ||
        (previous_dist < 0.0f && dist >= 0.0f)) {
        state->position = state->target;
        state->velocity = 0.0f;
        state->acceleration = 0.0f;
        state->done = 1;
        return state->position;
    }

    state->done = 0;
    return state->position;
}

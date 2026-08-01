/**
 * @file bm_algo_profile.c
 * @brief 轨迹规划：斜坡、梯形与 S 曲线实现
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.0            zeh            补齐 Doxygen 注释
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/algorithm/bm_algo_errors.h"
#include "bm/common/bm_types.h"
#include <stddef.h>

#include <math.h>

int bm_algo_ramp_validate_config(const bm_algo_ramp_config_t *config) {
    if (config == NULL ||
        !bm_algo_is_finite_f(config->rate_per_s) ||
        config->rate_per_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_algo_trapezoid_validate_config(const bm_algo_trapezoid_config_t *config) {
    if (config == NULL ||
        !bm_algo_is_finite_f(config->max_vel) || config->max_vel <= 0.0f ||
        !bm_algo_is_finite_f(config->max_accel) || config->max_accel <= 0.0f ||
        !bm_algo_is_finite_f(config->max_decel) || config->max_decel <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_algo_scurve_validate_config(const bm_algo_scurve_config_t *config) {
    if (config == NULL ||
        !bm_algo_is_finite_f(config->max_vel) || config->max_vel <= 0.0f ||
        !bm_algo_is_finite_f(config->max_accel) || config->max_accel <= 0.0f ||
        !bm_algo_is_finite_f(config->max_jerk) || config->max_jerk <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

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

    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        bm_algo_ramp_validate_config(config) != BM_OK) {
        return target;
    }
    /* target 非有限（NaN/Inf）时 delta 比较恒为 false，会落入 else 分支
     * 使 state->output 每拍单向漂移且 done 永不置位，属永久污染；
     * 跳过本次更新并保持旧输出 */
    if (!bm_algo_is_finite_f(target)) {
        return state->output;
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
    /* target 非有限（NaN/Inf）会永久污染 state->target，导致后续每次
     * step 都算不出有限的 dist/stop_dist；忽略非法目标，保持旧状态 */
    if (state != NULL && bm_algo_is_finite_f(target)) {
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
    float max_vel;
    float max_accel;
    float max_decel;

    if (state == NULL || config == NULL ||
        !bm_algo_is_finite_f(dt_s) || dt_s <= 0.0f) {
        return 0.0f;
    }

    /* 用 clamp_f 将 NaN/Inf 限制到安全区间值，避免手写的 <=0 比较对 NaN 失效 */
    max_vel = bm_algo_clamp_f(config->max_vel, 0.0f, INFINITY);
    max_accel = bm_algo_clamp_f(config->max_accel, 0.0f, INFINITY);
    max_decel = bm_algo_clamp_f(config->max_decel, 0.0f, INFINITY);
    if (max_vel <= 0.0f || max_accel <= 0.0f || max_decel <= 0.0f) {
        return 0.0f;
    }

    dist = state->target - state->position;
    accel = max_accel;
    decel = max_decel;

    if (fabsf(dist) < 1e-6f && fabsf(state->velocity) < 1e-6f) {
        state->velocity = 0.0f;
        state->done = 1;
        return state->position;
    }

    stop_dist = (state->velocity * state->velocity) / (2.0f * decel);

    if (dist >= 0.0f) {
        if (dist > stop_dist || state->velocity < 0.0f) {
            state->velocity += accel * dt_s;
            if (state->velocity > max_vel) {
                state->velocity = max_vel;
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
            if (state->velocity < -max_vel) {
                state->velocity = -max_vel;
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
        return state->position;
    }

    state->done = 0;

    /* phase  bookkeeping：运动中显式标记加速/匀速/减速阶段，
     * 并修正明显错误的阶段（如未 done 却停留在 3） */
    if (state->phase == 3) {
        state->phase = 0;
    }
    if (dist >= 0.0f) {
        if (dist > stop_dist || state->velocity < 0.0f) {
            state->phase = (state->velocity >= max_vel) ? 1 : 0;
        } else {
            state->phase = 2;
        }
    } else {
        if (-dist > stop_dist || state->velocity > 0.0f) {
            state->phase = (-state->velocity >= max_vel) ? 1 : 0;
        } else {
            state->phase = 2;
        }
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
    /* target 非有限（NaN/Inf）会永久污染 state->target，导致后续每次
     * step 都算不出有限的 dist/stopping_distance；忽略非法目标，保持旧状态 */
    if (state != NULL && bm_algo_is_finite_f(target)) {
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

    if (state == NULL || config == NULL ||
        !bm_algo_is_finite_f(dt_s) || dt_s <= 0.0f ||
        bm_algo_scurve_validate_config(config) != BM_OK) {
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

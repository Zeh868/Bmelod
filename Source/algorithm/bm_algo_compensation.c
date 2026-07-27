/**
 * @file bm_algo_compensation.c
 * @brief 执行器非线性补偿实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-07-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       1.2            zeh            修正背隙补偿只增不减缺陷：换向时重置 backlash_offset 为 0 后重新渐进，保持渐进语义
 * 2026-06-23       1.3            zeh            背隙补偿升级为双向独立偏移：正向/反向各维护 offset_fwd/offset_rev，换向时切换至对应偏移继续渐进
 * 2026-07-09       1.4            zeh            Medium-7：bm_algo_dob_step
 *                                                补 u/y 有限性护栏，避免一次
 *                                                NaN/Inf 污染 y_hat/disturbance
 *                                                持久状态
 * 2026-07-14       1.5            zeh            Medium-6 修复：friction_comp
 *                                                对非有限 velocity 返回 0，
 *                                                避免 NaN 力矩穿透到执行器
 * 2026-07-27       1.6            zeh            dob_step 的 lpf_alpha 改用
 *                                                bm_algo_lpf1_alpha_saturate
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_compensation.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_common.h"
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

    /* NaN 时 fabsf(NaN) < v_deadband 恒为 false，会错误地返回带 NaN 的力矩 */
    if (!bm_algo_is_finite_f(velocity)) {
        return 0.0f;
    }
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

    /* Medium-7：u/y 为 NaN/Inf 时会直接污染 y_hat/disturbance 持久状态
     * （config->plant_gain * u 或 y - y_hat 产生 NaN 后再被低通滤波
     * 永久扩散）；入口拒绝非有限输入，保持旧扰动估计不变（H9 同款护栏）。 */
    if (!bm_algo_is_finite_f(u) || !bm_algo_is_finite_f(y) ||
        !bm_algo_is_finite_f(config->plant_gain) ||
        config->plant_gain <= 0.0f) {
        if (disturbance_out != NULL) {
            *disturbance_out = state->disturbance;
        }
        return state->disturbance;
    }

    state->y_hat = config->plant_gain * u;
    residual = y - state->y_hat;
    alpha = bm_algo_lpf1_alpha_saturate(config->lpf_alpha);
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
        state->offset_fwd     = 0.0f;
        state->offset_rev     = 0.0f;
    }
}

float bm_algo_backlash_inverse(float command,
                               bm_algo_backlash_state_t *state,
                               float width,
                               float slope) {
    int    direction;
    float *p_offset; /* 指向当前方向偏移的指针 */
    float  out;

    if (state == NULL || !bm_algo_is_finite_f(width) ||
        !bm_algo_is_finite_f(slope) || width <= 0.0f || slope <= 0.0f) {
        return command;
    }

    if (command > 0.0f) {
        direction = 1;
    } else if (command < 0.0f) {
        direction = -1;
    } else {
        /* command == 0：保持上次方向，不渐进，不更新 last_direction */
        direction = state->last_direction;
        out = command;
        if (direction > 0) {
            out += state->offset_fwd;
        } else if (direction < 0) {
            out -= state->offset_rev;
        }
        return out;
    }

    /*
     * 双向独立偏移策略：
     * - 正向（direction == 1）使用 offset_fwd，反向使用 offset_rev。
     * - 换向时不清零：直接切换到另一方向已保存的偏移继续渐进。
     * - 首次调用（last_direction == 0）视为无换向，直接渐进当前方向偏移。
     */
    p_offset = (direction > 0) ? &state->offset_fwd : &state->offset_rev;

    /* 渐进累加：每步最多增加 slope，上限为 width */
    if (*p_offset < width) {
        *p_offset += slope;
        if (*p_offset > width) {
            *p_offset = width;
        }
    }

    state->last_direction = direction;

    out = command;
    if (direction > 0) {
        out += state->offset_fwd;
    } else {
        out -= state->offset_rev;
    }
    return out;
}

/**
 * @file bm_algo_motion.c
 * @brief 运动辅助：编码器与 DDA 实现
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加步进脉冲生成
 * 2026-06-23       1.1            zeh            补齐 Doxygen 注释
 * 2026-07-09       1.2            zeh            H6：encoder_diag_step 的
 *                                                delta 计算改 int64 提宽，
 *                                                避免计数跨 INT32 边界溢出
 * 2026-07-13       1.3            zeh            C9：encoder_update 速度改由
 *                                                本拍等效位移直接求得（消除
 *                                                大位置相减的灾难性抵消）；
 *                                                位置改 double 中间量计算
 * 2026-07-28       1.4            zeh            步进脉冲生成改为有限计数与
 *                                                容量上界循环，拒绝不可表示输入
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

/** 步进脉冲计数不可转换为 uint32_t 的相位阈值。 */
#define BM_ALGO_STEPPER_COUNT_LIMIT_F 4294967296.0f

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
    int64_t eff_delta;
    float counts_to_rad;

    if (state == NULL || config == NULL || config->counts_per_rev == 0u ||
        dt_s <= 0.0f) {
        return 0.0f;
    }

    delta = (int64_t)raw_count - (int64_t)state->prev_count;
    /* eff_delta = 本拍等效计数位移（含跨圈回绕修正），与 turns 增减严格对应 */
    eff_delta = delta;
    /* TODO(Batch-3/low-14)：turns 饱和（INT32_MAX/MIN）后，position_rad 基于
     * 冻结的 turns 会与 velocity_rad_s 失去同步；需改用 int64 turns 或饱和
     * 标记才能根治。当前 21.5 亿圈才触发，按设计债挂账处理。 */
    if (delta > (int64_t)(config->counts_per_rev / 2u)) {
        if (state->turns > INT32_MIN) {
            state->turns--;
        }
        eff_delta = delta - (int64_t)config->counts_per_rev;
    } else if (delta < -(int64_t)(config->counts_per_rev / 2u)) {
        if (state->turns < INT32_MAX) {
            state->turns++;
        }
        eff_delta = delta + (int64_t)config->counts_per_rev;
    }

    state->prev_count = raw_count;
    counts_to_rad = 2.0f * BM_ALGO_PI_F / (float)config->counts_per_rev;

    /* C9-速度：改由本拍等效计数位移直接求速。原式"新旧绝对位置相减"在
     * 连续旋转使 position_rad 无界增大后，两个巨大近等 float 相减发生
     * 灾难性抵消，速度量化噪声随圈数增长；eff_delta 恒为小量，精度与
     * 圈数无关。数学上与原式严格等价：pos_new - pos_old = eff_delta × c2r。 */
    state->velocity_rad_s = (float)eff_delta * counts_to_rad / dt_s;

    /* C9-位置：double 中间量消除 float32 下 turns×cpr+raw 的中间量化
     * （turns×cpr 超 2^24 后 float 加 raw 直接丢位）；字段类型保持 float
     * 不变，绝对位置的长程精度需求方应改用单圈量（prev_count）自行推导，
     * 见 motor_foc_sensored 电角度取法。 */
    {
        double pos_counts_d = (double)state->turns *
                                  (double)config->counts_per_rev +
                              (double)raw_count;

        state->position_rad = (float)(pos_counts_d *
            (2.0 * (double)BM_ALGO_PI_F / (double)config->counts_per_rev));
    }
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
    float phase_increment;
    float phase_total;
    float next_phase;
    double next_phase_d;
    int8_t dir;
    uint32_t available_steps;
    uint32_t emitted_steps;
    uint32_t i;
    int64_t next_position;

    if (state == NULL || config == NULL ||
        !bm_algo_is_finite_f(dt_s) || dt_s <= 0.0f ||
        !bm_algo_is_finite_f(state->phase) || state->phase < 0.0f ||
        !bm_algo_is_finite_f(config->max_velocity_steps_s)) {
        return 0u;
    }
    /* velocity_steps_s 非有限（NaN/Inf）时下方钳位/符号判断均为 false，
     * state->phase += fabsf(velocity)*dt_s 会把 NaN 永久写入相位状态，
     * 导致此后 while (phase>=1.0f) 恒假、脉冲永久停摆；提前拒绝本次输入 */
    if (!bm_algo_is_finite_f(velocity_steps_s)) {
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
    phase_increment = fabsf(velocity_steps_s) * dt_s;
    phase_total = state->phase + phase_increment;
    if (!bm_algo_is_finite_f(phase_increment) ||
        !bm_algo_is_finite_f(phase_total) ||
        phase_total >= BM_ALGO_STEPPER_COUNT_LIMIT_F) {
        return 0u;
    }

    available_steps = (uint32_t)phase_total;
    emitted_steps = (pulses != NULL && available_steps > max_pulses)
                        ? max_pulses
                        : available_steps;
    next_position = (int64_t)state->position_steps +
                    ((dir > 0) ? (int64_t)emitted_steps
                               : -(int64_t)emitted_steps);
    if (next_position > (int64_t)INT32_MAX ||
        next_position < (int64_t)INT32_MIN) {
        return 0u;
    }

    next_phase_d = (double)phase_total - (double)emitted_steps;
    next_phase = (float)next_phase_d;
    if (!bm_algo_is_finite_f(next_phase) || next_phase < 0.0f ||
        (double)next_phase != next_phase_d) {
        return 0u;
    }

    if (pulses != NULL) {
        for (i = 0u; i < emitted_steps; ++i) {
            pulses[i] = dir;
        }
    }
    state->phase = next_phase;
    state->position_steps = (int32_t)next_position;
    return emitted_steps;
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
    int64_t delta; /* H6：int32 直减在 raw_count/prev_count 跨越
                    * INT32_MAX/MIN 边界时溢出 UB，照 bm_algo_encoder_update
                    * 的做法用 int64 提宽规避 */

    if (state == NULL || config == NULL) {
        return faults;
    }

    delta = (int64_t)raw_count - (int64_t)state->prev_count;
    if (config->max_delta_per_step > 0 &&
        (delta > (int64_t)config->max_delta_per_step ||
         delta < -(int64_t)config->max_delta_per_step)) {
        faults |= BM_ALGO_ENCODER_FAULT_MISSED;
    }

    if (index_pulse_seen != 0) {
        faults |= BM_ALGO_ENCODER_FAULT_INDEX;
    }

    state->prev_count = raw_count;
    return faults;
}

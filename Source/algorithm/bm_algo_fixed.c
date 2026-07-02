/**
 * @file bm_algo_fixed.c
 * @brief Q31/Q15 定点算法实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.4
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 PID Q31 与 Biquad Q15
 * 2026-06-17       1.2            zeh            Q15 滑动平均/PID 与 Q31 迟滞
 * 2026-06-17       1.3            zeh            Q31 梯形速度曲线
 * 2026-06-17       1.4            zeh            Q31 LPF、Q15 中值/高通
 * 2026-06-17       1.5            zeh            Q31 微分器、Q15 包络/RMS
 * 2026-06-17       1.6            zeh            定点第七批：库仑/高通/滑动平均 Q31、死区/滞回 Q15
 * 2026-06-17       1.7            zeh            定点第八批：积分/限速/超前滞后 Q15、二阶 IIR Q31
 * 2026-06-17       1.8            zeh            定点第九批：DOB Q15、包络/RMS Q31、背隙逆补偿 Q31
 * 2026-06-17       1.9            zeh            定点第十批：微分/DOB/库仑/超前滞后/互补/前馈 Q15/Q31
 * 2026-06-17       2.0            zeh            定点第十一批：PI/PR/斜坡/梯形/冗余/速率/SOC 融合
 * 2026-06-17       2.1            zeh            定点第十二批：S 曲线/MPPT/信号质量/Wh 积分
 * 2026-06-17       2.2            zeh            定点第十四批：全族 Q31/Q15 后缀 API 收口
 * 2026-06-23       2.3            zeh            缺陷修复：abs_q15 INT16_MIN UB、Mahony Ki 积分持久化、rms_q31 溢出防护
 * 2026-06-23       2.4            zeh            磁链观测器包装启用 wc_rad_s 衰减截止频率；修正 BM_ALGO_SQRT3_Q31 为精确 Q30 值
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_fixed.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/algorithm/bm_algo_signal_quality.h"
#include "bm/algorithm/bm_algo_fusion.h"
#include "bm/algorithm/bm_algo_motion.h"
#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_resample.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/algorithm/bm_algo_power.h"
#include <stddef.h>

#include <limits.h>
#include <math.h>
#include <string.h>

bm_algo_q31_t bm_algo_clamp_q31(bm_algo_q31_t value,
                                bm_algo_q31_t min_v,
                                bm_algo_q31_t max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

bm_algo_q15_t bm_algo_clamp_q15(bm_algo_q15_t value,
                                bm_algo_q15_t min_v,
                                bm_algo_q15_t max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

bm_algo_q31_t bm_algo_float_to_q31(float value) {
    float scaled;

    if (isnan(value)) {
        return 0;
    }
    if (value >= 1.0f) {
        return BM_ALGO_Q31_ONE;
    }
    if (value <= -1.0f) {
        return (bm_algo_q31_t)INT32_MIN;
    }
    scaled = value * 2147483648.0f;
    if (scaled > 2147483647.0f) {
        return BM_ALGO_Q31_ONE;
    }
    if (scaled < -2147483648.0f) {
        return (bm_algo_q31_t)INT32_MIN;
    }
    return (bm_algo_q31_t)scaled;
}

float bm_algo_q31_to_float(bm_algo_q31_t value) {
    return (float)value / 2147483648.0f;
}

bm_algo_q15_t bm_algo_float_to_q15(float value) {
    float scaled;

    if (isnan(value)) {
        return 0;
    }
    if (value >= 1.0f) {
        return BM_ALGO_Q15_ONE;
    }
    if (value <= -1.0f) {
        return (bm_algo_q15_t)(-32768);
    }
    scaled = value * 32768.0f;
    if (scaled > 32767.0f) {
        return BM_ALGO_Q15_ONE;
    }
    if (scaled < -32768.0f) {
        return (bm_algo_q15_t)(-32768);
    }
    return (bm_algo_q15_t)scaled;
}

float bm_algo_q15_to_float(bm_algo_q15_t value) {
    return (float)value / 32768.0f;
}

void bm_algo_pi_q31_reset(bm_algo_pi_q31_state_t *state, bm_algo_q31_t output) {
    if (state != NULL) {
        state->integrator = 0;
        state->output = output;
    }
}

static bm_algo_q31_t saturate_q31_i64(int64_t value);
static bm_algo_q15_t saturate_q15_i32(int32_t value);

static bm_algo_q31_t mul_q31(bm_algo_q31_t a, bm_algo_q31_t b) {
    int64_t prod = (int64_t)a * (int64_t)b;
    int64_t scaled = prod >> 31;

    return saturate_q31_i64(scaled);
}

static bm_algo_q31_t saturate_q31_i64(int64_t value) {
    if (value > (int64_t)INT32_MAX) {
        return (bm_algo_q31_t)INT32_MAX;
    }
    if (value < (int64_t)INT32_MIN) {
        return (bm_algo_q31_t)INT32_MIN;
    }
    return (bm_algo_q31_t)value;
}

static bm_algo_q15_t saturate_q15_i32(int32_t value) {
    if (value > 32767) {
        return (bm_algo_q15_t)32767;
    }
    if (value < -32768) {
        return (bm_algo_q15_t)-32768;
    }
    return (bm_algo_q15_t)value;
}

static uint64_t magnitude_i64(int64_t value) {
    return (value < 0) ? (uint64_t)(-(value + 1)) + 1u
                       : (uint64_t)value;
}

static uint64_t magnitude_i32(int32_t value) {
    return (value < 0) ? (uint64_t)(-(int64_t)value)
                       : (uint64_t)value;
}

static bm_algo_q31_t div_q31(int64_t num, bm_algo_q31_t den) {
    uint64_t num_mag;
    uint64_t den_mag;
    uint64_t scaled;
    int negative;

    if (den == 0) {
        return 0;
    }

    negative = ((num < 0) != (den < 0));
    num_mag = magnitude_i64(num);
    den_mag = magnitude_i32(den);
    if (num_mag >= den_mag) {
        return negative ? (bm_algo_q31_t)INT32_MIN
                        : (bm_algo_q31_t)INT32_MAX;
    }

    scaled = (num_mag << 31u) / den_mag;
    if (negative) {
        return (scaled >= 0x80000000ull)
                   ? (bm_algo_q31_t)INT32_MIN
                   : (bm_algo_q31_t)(-(int64_t)scaled);
    }
    return (scaled > (uint64_t)INT32_MAX)
               ? (bm_algo_q31_t)INT32_MAX
               : (bm_algo_q31_t)scaled;
}

/**
 * @brief Q15 除法（被除数与除数均为 Q15 幅值语义）
 */
static bm_algo_q15_t div_q15(int32_t num, bm_algo_q15_t den) {
    uint32_t num_mag;
    uint32_t den_mag;
    uint32_t scaled;
    int negative;

    if (den == 0) {
        return 0;
    }

    negative = ((num < 0) != (den < 0));
    num_mag = (num < 0) ? (uint32_t)(-(int64_t)num) : (uint32_t)num;
    den_mag = (den < 0) ? (uint32_t)(-(int32_t)den) : (uint32_t)den;
    if (num_mag >= den_mag) {
        return negative ? (bm_algo_q15_t)-32768 : BM_ALGO_Q15_ONE;
    }

    scaled = (num_mag << 15u) / den_mag;
    if (negative) {
        return (scaled >= 0x8000u) ? (bm_algo_q15_t)-32768
                                   : (bm_algo_q15_t)(-(int32_t)scaled);
    }
    return (scaled > 32767u) ? BM_ALGO_Q15_ONE : (bm_algo_q15_t)scaled;
}

bm_algo_q31_t bm_algo_pi_q31_step(bm_algo_pi_q31_state_t *state,
                                  const bm_algo_pi_q31_config_t *config,
                                  bm_algo_q31_t error,
                                  bm_algo_q31_t dt_q31) {
    bm_algo_q31_t p_term;
    bm_algo_q31_t u_unsat;
    bm_algo_q31_t u_sat;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return 0;
    }

    p_term = mul_q31(config->kp, error);
    state->integrator = saturate_q31_i64(
        (int64_t)state->integrator + (int64_t)mul_q31(error, dt_q31));
    state->integrator = bm_algo_clamp_q31(state->integrator,
                                          config->integrator_min,
                                          config->integrator_max);

    {
        int64_t u_sum = (int64_t)p_term +
                        (int64_t)mul_q31(config->ki, state->integrator);

        if (u_sum > (int64_t)INT32_MAX) {
            u_unsat = (bm_algo_q31_t)INT32_MAX;
        } else if (u_sum < (int64_t)INT32_MIN) {
            u_unsat = (bm_algo_q31_t)INT32_MIN;
        } else {
            u_unsat = (bm_algo_q31_t)u_sum;
        }
    }
    u_sat = bm_algo_clamp_q31(u_unsat, config->out_min, config->out_max);

    if (config->ki != 0 && u_sat != u_unsat) {
        state->integrator = div_q31(
            (int64_t)u_sat - (int64_t)p_term, config->ki);
        state->integrator = bm_algo_clamp_q31(state->integrator,
                                              config->integrator_min,
                                              config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

void bm_algo_lpf1_q15_reset(bm_algo_lpf1_q15_state_t *state, bm_algo_q15_t output) {
    if (state != NULL) {
        state->output = output;
    }
}

bm_algo_q15_t bm_algo_lpf1_q15_step(bm_algo_lpf1_q15_state_t *state,
                                    const bm_algo_lpf1_q15_config_t *config,
                                    bm_algo_q15_t input) {
    int32_t delta;
    int32_t y;

    if (state == NULL || config == NULL) {
        return input;
    }

    delta = ((int32_t)input - (int32_t)state->output) *
            (int32_t)config->alpha_q15;
    y = (int32_t)state->output + (delta >> 15);
    state->output = saturate_q15_i32(y);
    return state->output;
}

void bm_algo_pid_q31_reset(bm_algo_pid_q31_state_t *state, bm_algo_q31_t output) {
    if (state != NULL) {
        state->integrator = 0;
        state->prev_error = 0;
        state->d_filtered = 0;
        state->output = output;
    }
}

bm_algo_q31_t bm_algo_pid_q31_step(bm_algo_pid_q31_state_t *state,
                                   const bm_algo_pid_q31_config_t *config,
                                   bm_algo_q31_t error,
                                   bm_algo_q31_t dt_q31) {
    bm_algo_q31_t p_term;
    bm_algo_q31_t d_raw;
    bm_algo_q31_t d_term;
    bm_algo_q31_t u_unsat;
    bm_algo_q31_t u_sat;
    bm_algo_q31_t alpha;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return 0;
    }

    p_term = mul_q31(config->kp, error);

    state->integrator = saturate_q31_i64(
        (int64_t)state->integrator + (int64_t)mul_q31(error, dt_q31));
    state->integrator = bm_algo_clamp_q31(state->integrator,
                                          config->integrator_min,
                                          config->integrator_max);

    d_raw = div_q31(
        (int64_t)error - (int64_t)state->prev_error, dt_q31);
    state->prev_error = error;

    alpha = bm_algo_clamp_q31(config->d_filter_alpha_q31, 0, BM_ALGO_Q31_ONE);
    state->d_filtered = saturate_q31_i64(
        (int64_t)state->d_filtered +
        (int64_t)mul_q31(
            alpha,
            saturate_q31_i64(
                (int64_t)d_raw - (int64_t)state->d_filtered)));
    d_term = mul_q31(config->kd, state->d_filtered);

    {
        int64_t u_sum = (int64_t)p_term +
                        (int64_t)mul_q31(config->ki, state->integrator) +
                        (int64_t)d_term;

        if (u_sum > (int64_t)INT32_MAX) {
            u_unsat = (bm_algo_q31_t)INT32_MAX;
        } else if (u_sum < (int64_t)INT32_MIN) {
            u_unsat = (bm_algo_q31_t)INT32_MIN;
        } else {
            u_unsat = (bm_algo_q31_t)u_sum;
        }
    }
    u_sat = bm_algo_clamp_q31(u_unsat, config->out_min, config->out_max);

    if (config->ki != 0 && u_sat != u_unsat) {
        state->integrator = div_q31(
            (int64_t)u_sat - (int64_t)p_term - (int64_t)d_term,
            config->ki);
        state->integrator = bm_algo_clamp_q31(state->integrator,
                                              config->integrator_min,
                                              config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

void bm_algo_biquad_q15_reset(bm_algo_biquad_q15_state_t *state) {
    if (state != NULL) {
        state->z1 = 0;
        state->z2 = 0;
    }
}

static bm_algo_q15_t mul_q15(bm_algo_q15_t a, bm_algo_q15_t b) {
    int32_t prod = (int32_t)a * (int32_t)b;

    return saturate_q15_i32(prod >> 15);
}

bm_algo_q15_t bm_algo_biquad_q15_step(bm_algo_biquad_q15_state_t *state,
                                      const bm_algo_biquad_q15_config_t *config,
                                      bm_algo_q15_t input) {
    bm_algo_q15_t output;
    int32_t acc;

    if (state == NULL || config == NULL) {
        return input;
    }

    acc = (int32_t)mul_q15(config->b0, input) + (int32_t)state->z1;
    output = saturate_q15_i32(acc);

    acc = (int32_t)mul_q15(config->b1, input) -
          (int32_t)mul_q15(config->a1, output) + (int32_t)state->z2;
    state->z1 = saturate_q15_i32(acc);

    acc = (int32_t)mul_q15(config->b2, input) -
          (int32_t)mul_q15(config->a2, output);
    state->z2 = saturate_q15_i32(acc);

    return output;
}

/* ---------- 电机控制 Q31 算法族 ---------- */

#define BM_ALGO_SQRT3_Q30     1860867071 /* √3 的 Q30 定点表示：round(1.7320508075688772 × 2^30)，
                                          * 与 Q31 信号相乘后右移 30 位得到 Q31 结果（√3·v）。 */
#define BM_ALGO_INV_SQRT3_Q31 1239850262 /* 1/sqrt(3) in Q31 = 0.577350269 */

void bm_algo_clarke_q31(const bm_algo_abc_q31_t *abc, bm_algo_alphabeta_q31_t *ab) {
    if (abc == NULL || ab == NULL) {
        return;
    }
    ab->i_alpha = abc->ia;
    int64_t sum = (int64_t)abc->ia + 2LL * (int64_t)abc->ib;
    int64_t beta = (sum * BM_ALGO_INV_SQRT3_Q31) >> 31;
    ab->i_beta = saturate_q31_i64(beta);
}

void bm_algo_clarke_2shunt_q31(bm_algo_q31_t ia, bm_algo_q31_t ib, bm_algo_alphabeta_q31_t *ab) {
    if (ab == NULL) {
        return;
    }
    ab->i_alpha = ia;
    int64_t sum = (int64_t)ia + 2LL * (int64_t)ib;
    int64_t beta = (sum * BM_ALGO_INV_SQRT3_Q31) >> 31;
    ab->i_beta = saturate_q31_i64(beta);
}

void bm_algo_park_q31(const bm_algo_alphabeta_q31_t *ab,
                      bm_algo_q31_t sin_theta,
                      bm_algo_q31_t cos_theta,
                      bm_algo_dq_q31_t *dq) {
    if (ab == NULL || dq == NULL) {
        return;
    }
    int64_t d = ((int64_t)ab->i_alpha * cos_theta + (int64_t)ab->i_beta * sin_theta) >> 31;
    int64_t q = (-(int64_t)ab->i_alpha * sin_theta + (int64_t)ab->i_beta * cos_theta) >> 31;
    dq->id = saturate_q31_i64(d);
    dq->iq = saturate_q31_i64(q);
}

void bm_algo_inv_park_q31(const bm_algo_dq_q31_t *dq,
                          bm_algo_q31_t sin_theta,
                          bm_algo_q31_t cos_theta,
                          bm_algo_alphabeta_q31_t *ab) {
    if (dq == NULL || ab == NULL) {
        return;
    }
    int64_t alpha = ((int64_t)dq->id * cos_theta - (int64_t)dq->iq * sin_theta) >> 31;
    int64_t beta  = ((int64_t)dq->id * sin_theta + (int64_t)dq->iq * cos_theta) >> 31;
    ab->i_alpha = saturate_q31_i64(alpha);
    ab->i_beta  = saturate_q31_i64(beta);
}

void bm_algo_svpwm_q31(bm_algo_q31_t v_alpha,
                       bm_algo_q31_t v_beta,
                       bm_algo_svpwm_q31_out_t *out) {
    int64_t va;
    int64_t vb;
    int64_t vc;
    int64_t v_max;
    int64_t v_min;
    int64_t v_offset;
    bm_algo_q31_t sqrt3_vbeta;

    if (out == NULL) {
        return;
    }

    sqrt3_vbeta = saturate_q31_i64(((int64_t)v_beta * BM_ALGO_SQRT3_Q30) >> 30);

    va = v_alpha;
    vb = (-v_alpha + sqrt3_vbeta) >> 1;
    vc = (-v_alpha - sqrt3_vbeta) >> 1;

    v_max = va;
    if (vb > v_max) v_max = vb;
    if (vc > v_max) v_max = vc;

    v_min = va;
    if (vb < v_min) v_min = vb;
    if (vc < v_min) v_min = vc;

    v_offset = -(v_max + v_min) / 2LL;
    va += v_offset;
    vb += v_offset;
    vc += v_offset;

    int64_t half = 1073741824LL;
    out->duty_a = saturate_q31_i64(half + (va >> 1));
    out->duty_b = saturate_q31_i64(half + (vb >> 1));
    out->duty_c = saturate_q31_i64(half + (vc >> 1));
}

void bm_algo_integrator_q31_reset(bm_algo_integrator_q31_state_t *state,
                                  bm_algo_q31_t value) {
    if (state != NULL) {
        state->integrator = value;
    }
}

bm_algo_q31_t bm_algo_integrator_q31_step(bm_algo_integrator_q31_state_t *state,
                                          const bm_algo_integrator_q31_config_t *config,
                                          bm_algo_q31_t input,
                                          bm_algo_q31_t dt_q31) {
    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return input;
    }

    state->integrator = saturate_q31_i64(
        (int64_t)state->integrator + (int64_t)mul_q31(input, dt_q31));
    state->integrator = bm_algo_clamp_q31(state->integrator,
                                          config->min,
                                          config->max);
    return state->integrator;
}

void bm_algo_rate_limit_q31_reset(bm_algo_rate_limit_q31_state_t *state,
                                  bm_algo_q31_t output) {
    if (state != NULL) {
        state->output = output;
    }
}

bm_algo_q31_t bm_algo_rate_limit_q31_step(bm_algo_rate_limit_q31_state_t *state,
                                          const bm_algo_rate_limit_q31_config_t *config,
                                          bm_algo_q31_t target,
                                          bm_algo_q31_t dt_q31) {
    bm_algo_q31_t delta;
    bm_algo_q31_t max_up;
    bm_algo_q31_t max_dn;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return target;
    }

    max_up = mul_q31(config->max_rise_per_s_q31, dt_q31);
    max_dn = mul_q31(config->max_fall_per_s_q31, dt_q31);
    delta = target - state->output;

    if (delta > max_up) {
        state->output = saturate_q31_i64((int64_t)state->output + (int64_t)max_up);
    } else if (delta < -max_dn) {
        state->output = saturate_q31_i64((int64_t)state->output - (int64_t)max_dn);
    } else {
        state->output = target;
    }
    return state->output;
}

bm_algo_q31_t bm_algo_deadband_q31(bm_algo_q31_t value, bm_algo_q31_t width_q31) {
    bm_algo_q31_t abs_v;

    if (width_q31 <= 0) {
        return value;
    }

    abs_v = (value < 0) ? (bm_algo_q31_t)(-(int64_t)value) : value;
    if (abs_v <= width_q31) {
        return 0;
    }
    if (value > 0) {
        return value - width_q31;
    }
    return value + width_q31;
}

void bm_algo_pr_q15_reset(bm_algo_pr_q15_state_t *state) {
    if (state != NULL) {
        state->x1 = 0;
        state->x2 = 0;
        state->y1 = 0;
        state->y2 = 0;
        state->output = 0;
    }
}

bm_algo_q15_t bm_algo_pr_q15_step(bm_algo_pr_q15_state_t *state,
                                  const bm_algo_pr_q15_config_t *config,
                                  bm_algo_q15_t error) {
    int32_t y;
    int32_t b0e;
    int32_t b1x1;
    int32_t b2x2;
    int32_t a1y1;
    int32_t a2y2;

    if (state == NULL || config == NULL) {
        return 0;
    }

    b0e = ((int32_t)config->b0 * (int32_t)error) >> 15;
    b1x1 = ((int32_t)config->b1 * (int32_t)state->x1) >> 15;
    b2x2 = ((int32_t)config->b2 * (int32_t)state->x2) >> 15;
    a1y1 = ((int32_t)config->a1 * (int32_t)state->y1) >> 15;
    a2y2 = ((int32_t)config->a2 * (int32_t)state->y2) >> 15;
    y = b0e + b1x1 + b2x2 - a1y1 - a2y2;

    state->x2 = state->x1;
    state->x1 = error;
    state->y2 = state->y1;
    state->y1 = saturate_q15_i32(y);
    state->output = bm_algo_clamp_q15(state->y1, config->out_min, config->out_max);
    return state->output;
}

void bm_algo_ramp_q31_reset(bm_algo_ramp_q31_state_t *state, bm_algo_q31_t output) {
    if (state != NULL) {
        state->output = output;
        state->done = 1;
    }
}

bm_algo_q31_t bm_algo_ramp_q31_step(bm_algo_ramp_q31_state_t *state,
                                    const bm_algo_ramp_q31_config_t *config,
                                    bm_algo_q31_t target,
                                    bm_algo_q31_t dt_q31) {
    bm_algo_q31_t delta;
    bm_algo_q31_t step;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return target;
    }

    delta = target - state->output;
    step = mul_q31(config->rate_per_s_q31, dt_q31);

    if (delta > step) {
        state->output = saturate_q31_i64((int64_t)state->output + (int64_t)step);
        state->done = 0;
    } else if (delta < -step) {
        state->output = saturate_q31_i64((int64_t)state->output - (int64_t)step);
        state->done = 0;
    } else {
        state->output = target;
        state->done = 1;
    }
    return state->output;
}

void bm_algo_scurve_q31_reset(bm_algo_scurve_q31_state_t *state,
                              bm_algo_q31_t position,
                              bm_algo_q31_t velocity,
                              bm_algo_q31_t acceleration) {
    if (state != NULL) {
        state->position = position;
        state->velocity = velocity;
        state->acceleration = acceleration;
        state->target = position;
        state->done = 1;
    }
}

void bm_algo_scurve_q31_set_target(bm_algo_scurve_q31_state_t *state,
                                   bm_algo_q31_t target) {
    if (state != NULL) {
        state->target = target;
        state->done = 0;
    }
}

bm_algo_q31_t bm_algo_scurve_q31_step(bm_algo_scurve_q31_state_t *state,
                                      const bm_algo_scurve_q31_config_t *config,
                                      bm_algo_q31_t dt_q31) {
    bm_algo_scurve_config_t fcfg;
    bm_algo_scurve_state_t fst;
    float pos;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return (state != NULL) ? state->position : 0;
    }

    fcfg.max_vel = bm_algo_q31_to_float(config->max_vel_q31);
    fcfg.max_accel = bm_algo_q31_to_float(config->max_accel_q31);
    fcfg.max_jerk = bm_algo_q31_to_float(config->max_jerk_q31);

    fst.position = bm_algo_q31_to_float(state->position);
    fst.velocity = bm_algo_q31_to_float(state->velocity);
    fst.acceleration = bm_algo_q31_to_float(state->acceleration);
    fst.target = bm_algo_q31_to_float(state->target);
    fst.done = state->done;

    pos = bm_algo_scurve_step(&fst, &fcfg, bm_algo_q31_to_float(dt_q31));

    state->position = bm_algo_float_to_q31(fst.position);
    state->velocity = bm_algo_float_to_q31(fst.velocity);
    state->acceleration = bm_algo_float_to_q31(fst.acceleration);
    state->done = fst.done;
    return bm_algo_float_to_q31(pos);
}

void bm_algo_moving_avg_q15_reset(bm_algo_moving_avg_q15_state_t *state) {
    uint32_t i;

    if (state == NULL) {
        return;
    }
    for (i = 0u; i < BM_ALGO_MOVING_AVG_Q15_MAX; ++i) {
        state->samples[i] = 0;
    }
    state->count = 0u;
    state->index = 0u;
}

bm_algo_q15_t bm_algo_moving_avg_q15_step(bm_algo_moving_avg_q15_state_t *state,
                                          const bm_algo_moving_avg_q15_config_t *config,
                                          bm_algo_q15_t input) {
    uint16_t win;
    int32_t sum = 0;
    uint16_t i;

    if (state == NULL || config == NULL || config->window_size == 0u) {
        return input;
    }

    win = config->window_size;
    if (win > BM_ALGO_MOVING_AVG_Q15_MAX) {
        win = BM_ALGO_MOVING_AVG_Q15_MAX;
    }

    state->samples[state->index] = input;
    state->index = (uint16_t)((state->index + 1u) % win);
    if (state->count < win) {
        state->count++;
    }

    for (i = 0u; i < state->count; ++i) {
        sum += (int32_t)state->samples[i];
    }
    return saturate_q15_i32(sum / (int32_t)state->count);
}

void bm_algo_pid_q15_reset(bm_algo_pid_q15_state_t *state, bm_algo_q15_t output) {
    if (state != NULL) {
        state->integrator = 0;
        state->prev_error = 0;
        state->d_filtered = 0;
        state->output = output;
    }
}

bm_algo_q15_t bm_algo_pid_q15_step(bm_algo_pid_q15_state_t *state,
                                   const bm_algo_pid_q15_config_t *config,
                                   bm_algo_q15_t error,
                                   bm_algo_q15_t dt_q15) {
    int32_t p_term;
    int32_t d_raw;
    int32_t d_term;
    int32_t u_unsat;
    bm_algo_q15_t u_sat;
    int32_t alpha;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return 0;
    }

    p_term = ((int32_t)config->kp * (int32_t)error) >> 15;

    state->integrator = saturate_q15_i32(
        (int32_t)state->integrator +
        (((int32_t)error * (int32_t)dt_q15) >> 15));
    state->integrator = bm_algo_clamp_q15(state->integrator,
                                          config->integrator_min,
                                          config->integrator_max);

    d_raw = (((int32_t)error - (int32_t)state->prev_error) << 15) /
            (int32_t)dt_q15;
    state->prev_error = error;

    alpha = (int32_t)bm_algo_clamp_q15(config->d_filter_alpha_q15,
                                       0, BM_ALGO_Q15_ONE);
    state->d_filtered = saturate_q15_i32(
        (int32_t)state->d_filtered +
        ((alpha * ((int32_t)d_raw - (int32_t)state->d_filtered)) >> 15));
    d_term = ((int32_t)config->kd * (int32_t)state->d_filtered) >> 15;

    u_unsat = p_term +
              (((int32_t)config->ki * (int32_t)state->integrator) >> 15) +
              d_term;
    u_sat = bm_algo_clamp_q15(saturate_q15_i32(u_unsat),
                              config->out_min, config->out_max);

    if (config->ki != 0 && u_sat != saturate_q15_i32(u_unsat)) {
        /* 反算抗饱和：全程 int64 运算，避免 p_term/d_term 强转 Q15 溢出
         * （kp=error=-32768 时 p_term=+32768 越 int16），最后统一饱和到 Q15。 */
        int64_t back = (((int64_t)u_sat - (int64_t)p_term - (int64_t)d_term)
                        << 15) / (int64_t)config->ki;
        state->integrator = saturate_q15_i32(saturate_q31_i64(back));
        state->integrator = bm_algo_clamp_q15(state->integrator,
                                              config->integrator_min,
                                              config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

void bm_algo_hysteresis_q31_reset(bm_algo_hysteresis_q31_state_t *state) {
    if (state != NULL) {
        state->output_on = 0;
    }
}

bm_algo_q31_t bm_algo_hysteresis_q31_step(bm_algo_hysteresis_q31_state_t *state,
                                          const bm_algo_hysteresis_q31_config_t *config,
                                          bm_algo_q31_t input) {
    if (state == NULL || config == NULL) {
        return 0;
    }

    if (!state->output_on) {
        if (input >= config->high_threshold) {
            state->output_on = 1;
        }
    } else if (input <= config->low_threshold) {
        state->output_on = 0;
    }

    return state->output_on ? BM_ALGO_Q31_ONE : 0;
}

void bm_algo_trapezoid_q31_reset(bm_algo_trapezoid_q31_state_t *state,
                                 bm_algo_q31_t position,
                                 bm_algo_q31_t velocity) {
    if (state != NULL) {
        state->position = position;
        state->velocity = velocity;
        state->target = position;
        state->done = 1;
    }
}

void bm_algo_trapezoid_q31_set_target(bm_algo_trapezoid_q31_state_t *state,
                                      bm_algo_q31_t target) {
    if (state != NULL) {
        state->target = target;
        state->done = 0;
    }
}

bm_algo_q31_t bm_algo_trapezoid_q31_step(bm_algo_trapezoid_q31_state_t *state,
                                        const bm_algo_trapezoid_q31_config_t *config,
                                        bm_algo_q31_t dt_q31) {
    bm_algo_q31_t dist;
    bm_algo_q31_t accel_step;
    bm_algo_q31_t decel_step;
    bm_algo_q31_t vel_step;
    bm_algo_q31_t pos_delta;
    int64_t stop_num;
    bm_algo_q31_t stop_dist;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return 0;
    }
    if (config->max_vel_q31 <= 0 || config->max_accel_q31 <= 0 ||
        config->max_decel_q31 <= 0) {
        return state->position;
    }

    dist = state->target - state->position;
    if (dist == 0 && state->velocity == 0) {
        state->done = 1;
        return state->position;
    }

    accel_step = mul_q31(config->max_accel_q31, dt_q31);
    decel_step = mul_q31(config->max_decel_q31, dt_q31);
    /* 刹车距离 v²/(2a)：stop_num 为 Q31 表示的 v_p²（v²>>31），
     * 除法前左移 31 补回定标，除以 (2·decel_q31) 得 Q31 刹车距离。 */
    stop_num = ((int64_t)state->velocity * (int64_t)state->velocity) >> 31;
    stop_dist = saturate_q31_i64(
        (stop_num << 31) / (2 * (int64_t)config->max_decel_q31));

    if (dist > 0) {
        if (dist > stop_dist || state->velocity < 0) {
            state->velocity += accel_step;
            if (state->velocity > config->max_vel_q31) {
                state->velocity = config->max_vel_q31;
            }
        } else {
            state->velocity -= decel_step;
            if (state->velocity < 0) {
                state->velocity = 0;
            }
        }
    } else if (dist < 0) {
        if (-dist > stop_dist || state->velocity > 0) {
            state->velocity -= accel_step;
            if (state->velocity < -config->max_vel_q31) {
                state->velocity = -config->max_vel_q31;
            }
        } else {
            state->velocity += decel_step;
            if (state->velocity > 0) {
                state->velocity = 0;
            }
        }
    }

    vel_step = mul_q31(state->velocity, dt_q31);
    pos_delta = vel_step;
    state->position = saturate_q31_i64((int64_t)state->position +
                                       (int64_t)pos_delta);

    dist = state->target - state->position;
    if ((dist >= 0 && state->velocity <= 0 && dist <= accel_step) ||
        (dist <= 0 && state->velocity >= 0 && -dist <= accel_step)) {
        state->position = state->target;
        state->velocity = 0;
        state->done = 1;
    }

    return state->position;
}

void bm_algo_lpf1_q31_reset(bm_algo_lpf1_q31_state_t *state, bm_algo_q31_t output) {
    if (state != NULL) {
        state->output = output;
    }
}

bm_algo_q31_t bm_algo_lpf1_q31_step(bm_algo_lpf1_q31_state_t *state,
                                    const bm_algo_lpf1_q31_config_t *config,
                                    bm_algo_q31_t input) {
    int64_t delta;
    int64_t y;

    if (state == NULL || config == NULL) {
        return input;
    }

    delta = ((int64_t)input - (int64_t)state->output) *
            (int64_t)config->alpha_q31;
    y = (int64_t)state->output + (delta >> 31);
    state->output = saturate_q31_i64(y);
    return state->output;
}

static bm_algo_q15_t median3_q15(bm_algo_q15_t a, bm_algo_q15_t b, bm_algo_q15_t c) {
    bm_algo_q15_t t;

    if (a > b) {
        t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        t = a;
        a = b;
        b = t;
    }
    return b;
}

void bm_algo_median3_q15_reset(bm_algo_median3_q15_state_t *state) {
    if (state != NULL) {
        state->samples[0] = 0;
        state->samples[1] = 0;
        state->samples[2] = 0;
        state->count = 0u;
    }
}

bm_algo_q15_t bm_algo_median3_q15_step(bm_algo_median3_q15_state_t *state,
                                       bm_algo_q15_t input) {
    bm_algo_q15_t out;

    if (state == NULL) {
        return input;
    }

    if (state->count < 3u) {
        state->samples[state->count] = input;
        state->count++;
        if (state->count == 1u) {
            return input;
        }
        if (state->count == 2u) {
            return saturate_q15_i32(
                ((int32_t)state->samples[0] + (int32_t)state->samples[1]) >> 1);
        }
    } else {
        state->samples[0] = state->samples[1];
        state->samples[1] = state->samples[2];
        state->samples[2] = input;
    }

    out = median3_q15(state->samples[0], state->samples[1], state->samples[2]);
    return out;
}

void bm_algo_hpf1_q15_reset(bm_algo_hpf1_q15_state_t *state) {
    if (state != NULL) {
        state->prev_input = 0;
        state->prev_output = 0;
    }
}

bm_algo_q15_t bm_algo_hpf1_q15_step(bm_algo_hpf1_q15_state_t *state,
                                    const bm_algo_hpf1_q15_config_t *config,
                                    bm_algo_q15_t input) {
    int32_t diff;
    int32_t sum;
    int32_t prod;
    int32_t out;

    if (state == NULL || config == NULL) {
        return input;
    }

    diff = (int32_t)input - (int32_t)state->prev_input;
    sum = (int32_t)state->prev_output + diff;
    prod = sum * (int32_t)config->alpha_q15;
    out = prod >> 15;
    out = saturate_q15_i32(out);
    state->prev_input = input;
    state->prev_output = (bm_algo_q15_t)out;
    return state->prev_output;
}

/**
 * @brief Q15 绝对值，对 INT16_MIN 饱和为 INT16_MAX（避免 -(-32768) 溢出 UB）
 * @param v 输入 Q15 值
 * @return 绝对值（饱和处理）
 */
static bm_algo_q15_t abs_q15(bm_algo_q15_t v) {
    if (v == (bm_algo_q15_t)INT16_MIN) {
        return (bm_algo_q15_t)INT16_MAX;
    }
    return (v < 0) ? (bm_algo_q15_t)(-(int32_t)v) : v;
}

void bm_algo_differentiator_q31_reset(bm_algo_differentiator_q31_state_t *state) {
    if (state != NULL) {
        state->prev_input = 0;
        state->derivative = 0;
    }
}

bm_algo_q31_t bm_algo_differentiator_q31_step(
    bm_algo_differentiator_q31_state_t *state,
    const bm_algo_differentiator_q31_config_t *config,
    bm_algo_q31_t input,
    bm_algo_q31_t dt_q31) {
    float raw_d;
    float alpha;
    float deriv_f;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return 0;
    }

    raw_d = (bm_algo_q31_to_float(input) -
             bm_algo_q31_to_float(state->prev_input)) /
            bm_algo_q31_to_float(dt_q31);
    state->prev_input = input;

    alpha = bm_algo_q31_to_float(
        bm_algo_clamp_q31(config->coeff_q31, 0, BM_ALGO_Q31_ONE));
    deriv_f = bm_algo_q31_to_float(state->derivative) +
              alpha * (raw_d - bm_algo_q31_to_float(state->derivative));
    state->derivative = bm_algo_float_to_q31(deriv_f);
    return state->derivative;
}

void bm_algo_envelope_q15_reset(bm_algo_envelope_q15_state_t *state,
                                bm_algo_q15_t output) {
    if (state != NULL) {
        state->envelope = output;
    }
}

bm_algo_q15_t bm_algo_envelope_q15_step(bm_algo_envelope_q15_state_t *state,
                                        const bm_algo_envelope_q15_config_t *config,
                                        bm_algo_q15_t input) {
    int32_t abs_in;
    int32_t alpha;
    int32_t delta;
    int32_t out;

    if (state == NULL || config == NULL) {
        return input;
    }

    abs_in = (int32_t)abs_q15(input);
    alpha = (int32_t)bm_algo_clamp_q15(config->alpha_q15, 0, BM_ALGO_Q15_ONE);
    delta = abs_in - (int32_t)state->envelope;
    out = (int32_t)state->envelope + ((alpha * delta) >> 15);
    state->envelope = saturate_q15_i32(out);
    return state->envelope;
}

void bm_algo_rms_q15_reset(bm_algo_rms_q15_state_t *state) {
    if (state != NULL) {
        memset(state->samples, 0, sizeof(state->samples));
        state->count = 0u;
        state->index = 0u;
    }
}

bm_algo_q15_t bm_algo_rms_q15_step(bm_algo_rms_q15_state_t *state,
                                   const bm_algo_rms_q15_config_t *config,
                                   bm_algo_q15_t input) {
    uint16_t win;
    uint32_t i;
    int64_t sum_sq = 0;
    int32_t rms;

    if (state == NULL || config == NULL || config->window_size == 0u) {
        return input;
    }

    win = config->window_size;
    if (win > BM_ALGO_RMS_Q15_MAX) {
        win = BM_ALGO_RMS_Q15_MAX;
    }

    if (state->count < win) {
        state->samples[state->count] = input;
        state->count++;
    } else {
        state->samples[state->index] = input;
        state->index++;
        if (state->index >= win) {
            state->index = 0u;
        }
    }

    for (i = 0u; i < state->count; ++i) {
        int32_t s = (int32_t)state->samples[i];
        sum_sq += (int64_t)s * (int64_t)s;
    }

    rms = (int32_t)sqrt((double)sum_sq / (double)state->count);
    if (rms > 32767) {
        rms = 32767;
    }
    return (bm_algo_q15_t)rms;
}

void bm_algo_coulomb_q31_reset(bm_algo_coulomb_q31_state_t *state,
                               bm_algo_q31_t soc_init) {
    if (state != NULL) {
        state->soc = soc_init;
    }
}

bm_algo_q31_t bm_algo_coulomb_q31_step(bm_algo_coulomb_q31_state_t *state,
                                       const bm_algo_coulomb_q31_config_t *config,
                                       bm_algo_q31_t current_q31,
                                       bm_algo_q31_t dt_q31) {
    bm_algo_q31_t delta;
    bm_algo_q31_t prod;

    if (state == NULL || config == NULL || dt_q31 <= 0 ||
        config->nominal_capacity_q31 <= 0) {
        return (state != NULL) ? state->soc : 0;
    }

    prod = mul_q31(mul_q31(current_q31, dt_q31), config->coulomb_efficiency_q31);
    delta = div_q31((int64_t)prod, config->nominal_capacity_q31);
    state->soc = saturate_q31_i64((int64_t)state->soc + (int64_t)delta);
    state->soc = bm_algo_clamp_q31(state->soc,
                                   config->soc_min,
                                   config->soc_max);
    return state->soc;
}

void bm_algo_hpf1_q31_reset(bm_algo_hpf1_q31_state_t *state) {
    if (state != NULL) {
        state->prev_input = 0;
        state->prev_output = 0;
    }
}

bm_algo_q31_t bm_algo_hpf1_q31_step(bm_algo_hpf1_q31_state_t *state,
                                    const bm_algo_hpf1_q31_config_t *config,
                                    bm_algo_q31_t input) {
    int64_t diff;
    int64_t sum;
    int64_t prod;

    if (state == NULL || config == NULL) {
        return input;
    }

    diff = (int64_t)input - (int64_t)state->prev_input;
    sum = (int64_t)state->prev_output + diff;
    prod = sum * (int64_t)config->alpha_q31;
    state->prev_input = input;
    state->prev_output = saturate_q31_i64(prod >> 31);
    return state->prev_output;
}

void bm_algo_moving_avg_q31_reset(bm_algo_moving_avg_q31_state_t *state) {
    uint32_t i;

    if (state == NULL) {
        return;
    }
    for (i = 0u; i < BM_ALGO_MOVING_AVG_Q31_MAX; ++i) {
        state->samples[i] = 0;
    }
    state->count = 0u;
    state->index = 0u;
}

bm_algo_q31_t bm_algo_moving_avg_q31_step(bm_algo_moving_avg_q31_state_t *state,
                                          const bm_algo_moving_avg_q31_config_t *config,
                                          bm_algo_q31_t input) {
    uint16_t win;
    int64_t sum = 0;
    uint16_t i;

    if (state == NULL || config == NULL || config->window_size == 0u) {
        return input;
    }

    win = config->window_size;
    if (win > BM_ALGO_MOVING_AVG_Q31_MAX) {
        win = BM_ALGO_MOVING_AVG_Q31_MAX;
    }

    state->samples[state->index] = input;
    state->index = (uint16_t)((state->index + 1u) % win);
    if (state->count < win) {
        state->count++;
    }

    for (i = 0u; i < state->count; ++i) {
        sum += (int64_t)state->samples[i];
    }
    return saturate_q31_i64(sum / (int64_t)state->count);
}

bm_algo_q15_t bm_algo_deadband_q15(bm_algo_q15_t value, bm_algo_q15_t width_q15) {
    int32_t abs_v;

    if (width_q15 <= 0) {
        return value;
    }

    abs_v = (value < 0) ? -(int32_t)value : (int32_t)value;
    if (abs_v <= (int32_t)width_q15) {
        return 0;
    }
    if (value > 0) {
        return (bm_algo_q15_t)((int32_t)value - (int32_t)width_q15);
    }
    return (bm_algo_q15_t)((int32_t)value + (int32_t)width_q15);
}

void bm_algo_hysteresis_q15_reset(bm_algo_hysteresis_q15_state_t *state) {
    if (state != NULL) {
        state->output_on = 0;
    }
}

bm_algo_q15_t bm_algo_hysteresis_q15_step(bm_algo_hysteresis_q15_state_t *state,
                                          const bm_algo_hysteresis_q15_config_t *config,
                                          bm_algo_q15_t input) {
    if (state == NULL || config == NULL) {
        return 0;
    }

    if (!state->output_on) {
        if (input >= config->high_threshold) {
            state->output_on = 1;
        }
    } else if (input <= config->low_threshold) {
        state->output_on = 0;
    }

    return state->output_on ? BM_ALGO_Q15_ONE : 0;
}

/* ---------- 通用定点扩展（第八批） ---------- */

void bm_algo_integrator_q15_reset(bm_algo_integrator_q15_state_t *state,
                                  bm_algo_q15_t value) {
    if (state != NULL) {
        state->integrator = value;
    }
}

bm_algo_q15_t bm_algo_integrator_q15_step(bm_algo_integrator_q15_state_t *state,
                                          const bm_algo_integrator_q15_config_t *config,
                                          bm_algo_q15_t input,
                                          bm_algo_q15_t dt_q15) {
    int32_t acc;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return input;
    }

    acc = (int32_t)state->integrator +
          (int32_t)mul_q15(input, dt_q15);
    state->integrator = bm_algo_clamp_q15(saturate_q15_i32(acc),
                                          config->min,
                                          config->max);
    return state->integrator;
}

void bm_algo_rate_limit_q15_reset(bm_algo_rate_limit_q15_state_t *state,
                                  bm_algo_q15_t output) {
    if (state != NULL) {
        state->output = output;
    }
}

bm_algo_q15_t bm_algo_rate_limit_q15_step(bm_algo_rate_limit_q15_state_t *state,
                                          const bm_algo_rate_limit_q15_config_t *config,
                                          bm_algo_q15_t target,
                                          bm_algo_q15_t dt_q15) {
    bm_algo_q15_t max_up;
    bm_algo_q15_t max_dn;
    int32_t acc;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return target;
    }

    max_up = mul_q15(config->max_rise_per_s_q15, dt_q15);
    max_dn = mul_q15(config->max_fall_per_s_q15, dt_q15);
    {
        int32_t delta = (int32_t)target - (int32_t)state->output;

        if (delta > (int32_t)max_up) {
            acc = (int32_t)state->output + (int32_t)max_up;
            state->output = saturate_q15_i32(acc);
        } else if (delta < -(int32_t)max_dn) {
            acc = (int32_t)state->output - (int32_t)max_dn;
            state->output = saturate_q15_i32(acc);
        } else {
            state->output = target;
        }
    }
    return state->output;
}

int bm_algo_lead_lag_q15_init(bm_algo_lead_lag_q15_state_t *state,
                              const bm_algo_lead_lag_q15_config_t *config) {
    if (state == NULL || config == NULL) {
        return -1;
    }
    bm_algo_lead_lag_q15_reset(state);
    return 0;
}

void bm_algo_lead_lag_q15_reset(bm_algo_lead_lag_q15_state_t *state) {
    if (state != NULL) {
        state->x1 = 0;
        state->y1 = 0;
    }
}

bm_algo_q15_t bm_algo_lead_lag_q15_step(bm_algo_lead_lag_q15_state_t *state,
                                        const bm_algo_lead_lag_q15_config_t *config,
                                        bm_algo_q15_t input) {
    int32_t acc;
    bm_algo_q15_t output;

    if (state == NULL || config == NULL) {
        return input;
    }

    acc = (int32_t)mul_q15(config->b0, input) +
          (int32_t)mul_q15(config->b1, state->x1) -
          (int32_t)mul_q15(config->a1, state->y1);
    output = saturate_q15_i32(acc);
    state->x1 = input;
    state->y1 = output;
    return output;
}

void bm_algo_biquad_q31_reset(bm_algo_biquad_q31_state_t *state) {
    if (state != NULL) {
        state->z1 = 0;
        state->z2 = 0;
    }
}

bm_algo_q31_t bm_algo_biquad_q31_step(bm_algo_biquad_q31_state_t *state,
                                      const bm_algo_biquad_q31_config_t *config,
                                      bm_algo_q31_t input) {
    bm_algo_q31_t output;
    int64_t acc;

    if (state == NULL || config == NULL) {
        return input;
    }

    acc = (int64_t)mul_q31(config->b0, input) + (int64_t)state->z1;
    output = saturate_q31_i64(acc);

    acc = (int64_t)mul_q31(config->b1, input) -
          (int64_t)mul_q31(config->a1, output) + (int64_t)state->z2;
    state->z1 = saturate_q31_i64(acc);

    acc = (int64_t)mul_q31(config->b2, input) -
          (int64_t)mul_q31(config->a2, output);
    state->z2 = saturate_q31_i64(acc);

    return output;
}

void bm_algo_dob_q15_reset(bm_algo_dob_q15_state_t *state) {
    if (state != NULL) {
        state->y_hat = 0;
        state->disturbance = 0;
    }
}

bm_algo_q15_t bm_algo_dob_q15_step(bm_algo_dob_q15_state_t *state,
                                   const bm_algo_dob_q15_config_t *config,
                                   bm_algo_q15_t u_q15,
                                   bm_algo_q15_t y_q15,
                                   bm_algo_q15_t *disturbance_out) {
    bm_algo_q15_t alpha;
    bm_algo_q15_t residual;
    bm_algo_q15_t one_minus_alpha;
    int32_t blend;

    if (state == NULL || config == NULL) {
        if (disturbance_out != NULL) {
            *disturbance_out = 0;
        }
        return 0;
    }

    state->y_hat = mul_q15(config->plant_gain_q15, u_q15);
    residual = saturate_q15_i32((int32_t)y_q15 - (int32_t)state->y_hat);

    alpha = bm_algo_clamp_q15(config->lpf_alpha_q15, 0, BM_ALGO_Q15_ONE);
    one_minus_alpha = saturate_q15_i32((int32_t)BM_ALGO_Q15_ONE - (int32_t)alpha);
    blend = (int32_t)mul_q15(alpha, residual) +
            (int32_t)mul_q15(one_minus_alpha, state->disturbance);
    state->disturbance = saturate_q15_i32(blend);

    if (disturbance_out != NULL) {
        *disturbance_out = state->disturbance;
    }
    return state->disturbance;
}

void bm_algo_envelope_q31_reset(bm_algo_envelope_q31_state_t *state,
                                bm_algo_q31_t output) {
    if (state != NULL) {
        state->envelope = output;
    }
}

static bm_algo_q31_t abs_q31(bm_algo_q31_t v) {
    return (v < 0) ? (bm_algo_q31_t)(-(int64_t)v) : v;
}

bm_algo_q31_t bm_algo_envelope_q31_step(bm_algo_envelope_q31_state_t *state,
                                        const bm_algo_envelope_q31_config_t *config,
                                        bm_algo_q31_t input) {
    int64_t abs_in;
    bm_algo_q31_t alpha;
    int64_t delta;
    int64_t out;

    if (state == NULL || config == NULL) {
        return input;
    }

    abs_in = (int64_t)abs_q31(input);
    alpha = bm_algo_clamp_q31(config->alpha_q31, 0, BM_ALGO_Q31_ONE);
    delta = abs_in - (int64_t)state->envelope;
    out = (int64_t)state->envelope + (((int64_t)alpha * delta) >> 31);
    state->envelope = saturate_q31_i64(out);
    return state->envelope;
}

void bm_algo_rms_q31_reset(bm_algo_rms_q31_state_t *state) {
    if (state != NULL) {
        memset(state->samples, 0, sizeof(state->samples));
        state->count = 0u;
        state->index = 0u;
    }
}

bm_algo_q31_t bm_algo_rms_q31_step(bm_algo_rms_q31_state_t *state,
                                   const bm_algo_rms_q31_config_t *config,
                                   bm_algo_q31_t input) {
    uint16_t win;
    uint32_t i;
    uint64_t sum_sq = 0u;
    uint64_t rms;

    if (state == NULL || config == NULL || config->window_size == 0u) {
        return input;
    }

    win = config->window_size;
    if (win > BM_ALGO_RMS_Q31_MAX) {
        win = BM_ALGO_RMS_Q31_MAX;
    }

    if (state->count < win) {
        state->samples[state->count] = input;
        state->count++;
    } else {
        state->samples[state->index] = input;
        state->index++;
        if (state->index >= win) {
            state->index = 0u;
        }
    }

    /* 量程说明：BM_ALGO_RMS_Q31_MAX=16，INT32_MAX^2≈4.61e18，16×4.61e18≈7.38e19
     * 超过 uint64 上限（1.84e19）。对每个样本平方右移 4 位（缩小 16 倍）后累加，
     * 开方后乘以 4（=sqrt(16)）补回，避免累加器溢出。 */
    for (i = 0u; i < state->count; ++i) {
        int64_t s = (int64_t)state->samples[i];
        sum_sq += (uint64_t)((s * s) >> 4);
    }

    rms = (uint64_t)(sqrt((double)sum_sq / (double)state->count) * 4.0);
    if (rms > (uint64_t)INT32_MAX) {
        return (bm_algo_q31_t)INT32_MAX;
    }
    return (bm_algo_q31_t)rms;
}

void bm_algo_backlash_q31_reset(bm_algo_backlash_q31_state_t *state) {
    if (state != NULL) {
        state->last_direction = 0;
        state->offset_fwd = 0;
        state->offset_rev = 0;
    }
}

bm_algo_q31_t bm_algo_backlash_inverse_q31(bm_algo_q31_t command_q31,
                                           bm_algo_backlash_q31_state_t *state,
                                           bm_algo_q31_t width_q31,
                                           bm_algo_q31_t slope_q31) {
    int direction;
    bm_algo_q31_t *p_offset; /* 指向当前方向偏移 */
    bm_algo_q31_t out;

    if (state == NULL || width_q31 <= 0 || slope_q31 <= 0) {
        return command_q31;
    }

    if (command_q31 > 0) {
        direction = 1;
    } else if (command_q31 < 0) {
        direction = -1;
    } else {
        /* command == 0：保持上次方向，不渐进，不更新 last_direction */
        out = command_q31;
        if (state->last_direction > 0) {
            out = saturate_q31_i64((int64_t)out + (int64_t)state->offset_fwd);
        } else if (state->last_direction < 0) {
            out = saturate_q31_i64((int64_t)out - (int64_t)state->offset_rev);
        }
        return out;
    }

    /*
     * 双向独立偏移策略（对齐 float v1.3）：
     * - 正向（direction == 1）用 offset_fwd，反向用 offset_rev。
     * - 换向时不清零：切换到另一方向已保存的偏移继续渐进。
     * - 首次调用（last_direction == 0）视为无换向，直接渐进当前方向偏移。
     */
    p_offset = (direction > 0) ? &state->offset_fwd : &state->offset_rev;

    /* 渐进累加：每步最多增加 slope，上限为 width */
    if (*p_offset < width_q31) {
        *p_offset = saturate_q31_i64((int64_t)*p_offset + (int64_t)slope_q31);
        if (*p_offset > width_q31) {
            *p_offset = width_q31;
        }
    }

    state->last_direction = direction;

    out = command_q31;
    if (direction > 0) {
        out = saturate_q31_i64((int64_t)out + (int64_t)state->offset_fwd);
    } else {
        out = saturate_q31_i64((int64_t)out - (int64_t)state->offset_rev);
    }
    return out;
}

/* ---------- 通用定点扩展（第十批） ---------- */

void bm_algo_differentiator_q15_reset(bm_algo_differentiator_q15_state_t *state) {
    if (state != NULL) {
        state->prev_input = 0;
        state->derivative = 0;
    }
}

bm_algo_q15_t bm_algo_differentiator_q15_step(
    bm_algo_differentiator_q15_state_t *state,
    const bm_algo_differentiator_q15_config_t *config,
    bm_algo_q15_t input,
    bm_algo_q15_t dt_q15) {
    float raw_d;
    float alpha;
    float deriv_f;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return 0;
    }

    raw_d = (bm_algo_q15_to_float(input) -
             bm_algo_q15_to_float(state->prev_input)) /
            bm_algo_q15_to_float(dt_q15);
    state->prev_input = input;

    alpha = bm_algo_q15_to_float(
        bm_algo_clamp_q15(config->coeff_q15, 0, BM_ALGO_Q15_ONE));
    deriv_f = bm_algo_q15_to_float(state->derivative) +
              alpha * (raw_d - bm_algo_q15_to_float(state->derivative));
    state->derivative = bm_algo_float_to_q15(deriv_f);
    return state->derivative;
}

void bm_algo_dob_q31_reset(bm_algo_dob_q31_state_t *state) {
    if (state != NULL) {
        state->y_hat = 0;
        state->disturbance = 0;
    }
}

bm_algo_q31_t bm_algo_dob_q31_step(bm_algo_dob_q31_state_t *state,
                                   const bm_algo_dob_q31_config_t *config,
                                   bm_algo_q31_t u_q31,
                                   bm_algo_q31_t y_q31,
                                   bm_algo_q31_t *disturbance_out) {
    bm_algo_q31_t alpha;
    bm_algo_q31_t residual;
    bm_algo_q31_t one_minus_alpha;
    int64_t blend;

    if (state == NULL || config == NULL) {
        if (disturbance_out != NULL) {
            *disturbance_out = 0;
        }
        return 0;
    }

    state->y_hat = mul_q31(config->plant_gain_q31, u_q31);
    residual = saturate_q31_i64((int64_t)y_q31 - (int64_t)state->y_hat);

    alpha = bm_algo_clamp_q31(config->lpf_alpha_q31, 0, BM_ALGO_Q31_ONE);
    one_minus_alpha = saturate_q31_i64((int64_t)BM_ALGO_Q31_ONE - (int64_t)alpha);
    blend = (int64_t)mul_q31(alpha, residual) +
            (int64_t)mul_q31(one_minus_alpha, state->disturbance);
    state->disturbance = saturate_q31_i64(blend);

    if (disturbance_out != NULL) {
        *disturbance_out = state->disturbance;
    }
    return state->disturbance;
}

void bm_algo_coulomb_q15_reset(bm_algo_coulomb_q15_state_t *state,
                               bm_algo_q15_t soc_init) {
    if (state != NULL) {
        state->soc = soc_init;
    }
}

bm_algo_q15_t bm_algo_coulomb_q15_step(bm_algo_coulomb_q15_state_t *state,
                                       const bm_algo_coulomb_q15_config_t *config,
                                       bm_algo_q15_t current_q15,
                                       bm_algo_q15_t dt_q15) {
    bm_algo_q15_t prod;
    bm_algo_q15_t delta;

    if (state == NULL || config == NULL || dt_q15 <= 0 ||
        config->nominal_capacity_q15 <= 0) {
        return (state != NULL) ? state->soc : 0;
    }

    prod = mul_q15(mul_q15(current_q15, dt_q15), config->coulomb_efficiency_q15);
    delta = div_q15(prod, config->nominal_capacity_q15);
    state->soc = saturate_q15_i32((int32_t)state->soc + (int32_t)delta);
    state->soc = bm_algo_clamp_q15(state->soc,
                                   config->soc_min,
                                   config->soc_max);
    return state->soc;
}

int bm_algo_lead_lag_q31_init(bm_algo_lead_lag_q31_state_t *state,
                              const bm_algo_lead_lag_q31_config_t *config) {
    if (state == NULL || config == NULL) {
        return -1;
    }
    bm_algo_lead_lag_q31_reset(state);
    return 0;
}

void bm_algo_lead_lag_q31_reset(bm_algo_lead_lag_q31_state_t *state) {
    if (state != NULL) {
        state->x1 = 0;
        state->y1 = 0;
    }
}

bm_algo_q31_t bm_algo_lead_lag_q31_step(bm_algo_lead_lag_q31_state_t *state,
                                        const bm_algo_lead_lag_q31_config_t *config,
                                        bm_algo_q31_t input) {
    int64_t acc;
    bm_algo_q31_t output;

    if (state == NULL || config == NULL) {
        return input;
    }

    acc = (int64_t)mul_q31(config->b0, input) +
          (int64_t)mul_q31(config->b1, state->x1) -
          (int64_t)mul_q31(config->a1, state->y1);
    output = saturate_q31_i64(acc);
    state->x1 = input;
    state->y1 = output;
    return output;
}

void bm_algo_complementary_q15_reset(bm_algo_complementary_q15_state_t *state) {
    if (state != NULL) {
        state->roll_rad = 0;
        state->pitch_rad = 0;
    }
}

void bm_algo_complementary_q15_step(bm_algo_complementary_q15_state_t *state,
                                    const bm_algo_complementary_q15_config_t *config,
                                    bm_algo_q15_t gx_q15,
                                    bm_algo_q15_t gy_q15,
                                    bm_algo_q15_t gz_q15,
                                    bm_algo_q15_t ax_q15,
                                    bm_algo_q15_t ay_q15,
                                    bm_algo_q15_t az_q15,
                                    bm_algo_q15_t dt_q15) {
    float roll_gyro;
    float pitch_gyro;
    float roll_acc;
    float pitch_acc;
    float alpha;
    float dt_s;

    (void)gz_q15;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return;
    }

    dt_s = bm_algo_q15_to_float(dt_q15);
    roll_gyro = bm_algo_q15_to_float(state->roll_rad) +
                bm_algo_q15_to_float(gx_q15) * dt_s;
    pitch_gyro = bm_algo_q15_to_float(state->pitch_rad) +
                 bm_algo_q15_to_float(gy_q15) * dt_s;

    roll_acc = atan2f(bm_algo_q15_to_float(ay_q15), bm_algo_q15_to_float(az_q15));
    pitch_acc = atan2f(-bm_algo_q15_to_float(ax_q15),
                       sqrtf(bm_algo_q15_to_float(ay_q15) *
                                 bm_algo_q15_to_float(ay_q15) +
                             bm_algo_q15_to_float(az_q15) *
                                 bm_algo_q15_to_float(az_q15)));

    alpha = bm_algo_q15_to_float(
        bm_algo_clamp_q15(config->alpha_q15, 0, BM_ALGO_Q15_ONE));
    state->roll_rad = bm_algo_float_to_q15(
        alpha * roll_gyro + (1.0f - alpha) * roll_acc);
    state->pitch_rad = bm_algo_float_to_q15(
        alpha * pitch_gyro + (1.0f - alpha) * pitch_acc);
}

bm_algo_q31_t bm_algo_feedforward_q31_step(bm_algo_q31_t reference_q31,
                                           bm_algo_q31_t gain_q31,
                                           bm_algo_q31_t bias_q31) {
    int64_t acc;

    acc = (int64_t)mul_q31(reference_q31, gain_q31) + (int64_t)bias_q31;
    return saturate_q31_i64(acc);
}

bm_algo_q15_t bm_algo_feedforward_q15_step(bm_algo_q15_t reference_q15,
                                          bm_algo_q15_t gain_q15,
                                          bm_algo_q15_t bias_q15) {
    int32_t acc;

    acc = (int32_t)mul_q15(reference_q15, gain_q15) + (int32_t)bias_q15;
    return saturate_q15_i32(acc);
}

/* ---------- 通用定点扩展（第十一批） ---------- */

void bm_algo_pi_q15_reset(bm_algo_pi_q15_state_t *state, bm_algo_q15_t output) {
    if (state != NULL) {
        state->integrator = 0;
        state->output = output;
    }
}

bm_algo_q15_t bm_algo_pi_q15_step(bm_algo_pi_q15_state_t *state,
                                  const bm_algo_pi_q15_config_t *config,
                                  bm_algo_q15_t error,
                                  bm_algo_q15_t dt_q15) {
    bm_algo_q15_t p_term;
    bm_algo_q15_t u_unsat;
    bm_algo_q15_t u_sat;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return 0;
    }

    p_term = mul_q15(config->kp, error);
    state->integrator = saturate_q15_i32(
        (int32_t)state->integrator + (int32_t)mul_q15(error, dt_q15));
    state->integrator = bm_algo_clamp_q15(state->integrator,
                                          config->integrator_min,
                                          config->integrator_max);

    u_unsat = saturate_q15_i32((int32_t)p_term +
                               (int32_t)mul_q15(config->ki, state->integrator));
    u_sat = bm_algo_clamp_q15(u_unsat, config->out_min, config->out_max);

    if (config->ki != 0 && u_sat != u_unsat) {
        state->integrator = div_q15((int32_t)u_sat - (int32_t)p_term, config->ki);
        state->integrator = bm_algo_clamp_q15(state->integrator,
                                              config->integrator_min,
                                              config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

void bm_algo_pr_q31_reset(bm_algo_pr_q31_state_t *state) {
    if (state != NULL) {
        state->x1 = 0;
        state->x2 = 0;
        state->y1 = 0;
        state->y2 = 0;
        state->output = 0;
    }
}

bm_algo_q31_t bm_algo_pr_q31_step(bm_algo_pr_q31_state_t *state,
                                  const bm_algo_pr_q31_config_t *config,
                                  bm_algo_q31_t error) {
    int64_t y;
    int64_t b0e;
    int64_t b1x1;
    int64_t b2x2;
    int64_t a1y1;
    int64_t a2y2;

    if (state == NULL || config == NULL) {
        return 0;
    }

    b0e = ((int64_t)config->b0 * (int64_t)error) >> 31;
    b1x1 = ((int64_t)config->b1 * (int64_t)state->x1) >> 31;
    b2x2 = ((int64_t)config->b2 * (int64_t)state->x2) >> 31;
    a1y1 = ((int64_t)config->a1 * (int64_t)state->y1) >> 31;
    a2y2 = ((int64_t)config->a2 * (int64_t)state->y2) >> 31;
    y = b0e + b1x1 + b2x2 - a1y1 - a2y2;

    state->x2 = state->x1;
    state->x1 = error;
    state->y2 = state->y1;
    state->y1 = saturate_q31_i64(y);
    state->output = bm_algo_clamp_q31(state->y1, config->out_min, config->out_max);
    return state->output;
}

void bm_algo_ramp_q15_reset(bm_algo_ramp_q15_state_t *state, bm_algo_q15_t output) {
    if (state != NULL) {
        state->output = output;
        state->done = 1;
    }
}

bm_algo_q15_t bm_algo_ramp_q15_step(bm_algo_ramp_q15_state_t *state,
                                    const bm_algo_ramp_q15_config_t *config,
                                    bm_algo_q15_t target,
                                    bm_algo_q15_t dt_q15) {
    bm_algo_q15_t delta;
    bm_algo_q15_t step;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return target;
    }

    delta = target - state->output;
    step = mul_q15(config->rate_per_s_q15, dt_q15);

    if (delta > step) {
        state->output = saturate_q15_i32((int32_t)state->output + (int32_t)step);
        state->done = 0;
    } else if (delta < -step) {
        state->output = saturate_q15_i32((int32_t)state->output - (int32_t)step);
        state->done = 0;
    } else {
        state->output = target;
        state->done = 1;
    }
    return state->output;
}

void bm_algo_trapezoid_q15_reset(bm_algo_trapezoid_q15_state_t *state,
                                 bm_algo_q15_t position,
                                 bm_algo_q15_t velocity) {
    if (state != NULL) {
        state->position = position;
        state->velocity = velocity;
        state->target = position;
        state->done = 1;
    }
}

void bm_algo_trapezoid_q15_set_target(bm_algo_trapezoid_q15_state_t *state,
                                      bm_algo_q15_t target) {
    if (state != NULL) {
        state->target = target;
        state->done = 0;
    }
}

bm_algo_q15_t bm_algo_trapezoid_q15_step(bm_algo_trapezoid_q15_state_t *state,
                                         const bm_algo_trapezoid_q15_config_t *config,
                                         bm_algo_q15_t dt_q15) {
    bm_algo_q15_t dist;
    bm_algo_q15_t accel_step;
    bm_algo_q15_t decel_step;
    bm_algo_q15_t vel_step;
    bm_algo_q15_t pos_delta;
    int32_t stop_num;
    bm_algo_q15_t stop_dist;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return 0;
    }
    if (config->max_vel_q15 <= 0 || config->max_accel_q15 <= 0 ||
        config->max_decel_q15 <= 0) {
        return state->position;
    }

    dist = state->target - state->position;
    if (dist == 0 && state->velocity == 0) {
        state->done = 1;
        return state->position;
    }

    accel_step = mul_q15(config->max_accel_q15, dt_q15);
    decel_step = mul_q15(config->max_decel_q15, dt_q15);
    /* 刹车距离 v²/(2a)：stop_num 为 Q15 表示的 v_p²（v²>>15），
     * 除法前左移 15 补回定标，除以 (2·decel_q15) 得 Q15 刹车距离。
     * 用 int64 中间量防左移溢出。 */
    stop_num = ((int32_t)state->velocity * (int32_t)state->velocity) >> 15;
    stop_dist = saturate_q15_i32(
        (int32_t)(((int64_t)stop_num << 15) /
                  (2 * (int64_t)config->max_decel_q15)));

    if (dist > 0) {
        if (dist > stop_dist || state->velocity < 0) {
            state->velocity = saturate_q15_i32((int32_t)state->velocity +
                                               (int32_t)accel_step);
            if (state->velocity > config->max_vel_q15) {
                state->velocity = config->max_vel_q15;
            }
        } else {
            state->velocity = saturate_q15_i32((int32_t)state->velocity -
                                               (int32_t)decel_step);
            if (state->velocity < 0) {
                state->velocity = 0;
            }
        }
    } else if (dist < 0) {
        if (-dist > stop_dist || state->velocity > 0) {
            state->velocity = saturate_q15_i32((int32_t)state->velocity -
                                               (int32_t)accel_step);
            if (state->velocity < -config->max_vel_q15) {
                state->velocity = -config->max_vel_q15;
            }
        } else {
            state->velocity = saturate_q15_i32((int32_t)state->velocity +
                                               (int32_t)decel_step);
            if (state->velocity > 0) {
                state->velocity = 0;
            }
        }
    }

    vel_step = mul_q15(state->velocity, dt_q15);
    pos_delta = vel_step;
    state->position = saturate_q15_i32((int32_t)state->position +
                                       (int32_t)pos_delta);

    dist = state->target - state->position;
    if ((dist >= 0 && state->velocity <= 0 && dist <= accel_step) ||
        (dist <= 0 && state->velocity >= 0 && -dist <= accel_step)) {
        state->position = state->target;
        state->velocity = 0;
        state->done = 1;
    }

    return state->position;
}

static bm_algo_q15_t abs_q15_val(bm_algo_q15_t v) {
    return (v < 0) ? (bm_algo_q15_t)(-(int32_t)v) : v;
}

static bm_algo_q31_t abs_q31_val(bm_algo_q31_t v) {
    if (v == (bm_algo_q31_t)INT32_MIN) {
        return (bm_algo_q31_t)INT32_MAX;
    }
    return (v < 0) ? (bm_algo_q31_t)(-(int64_t)v) : v;
}

uint32_t bm_algo_redundant_pair_q15_step(
    bm_algo_q15_t a,
    bm_algo_q15_t b,
    const bm_algo_redundant_pair_q15_config_t *config) {
    bm_algo_q15_t diff;
    bm_algo_q15_t ref;
    bm_algo_q15_t tol;

    diff = abs_q15_val(a - b);
    if (config == NULL) {
        return (diff > 0) ? BM_ALGO_FAULT_REDUNDANT_MISMATCH : 0u;
    }

    ref = abs_q15_val(a);
    if (abs_q15_val(b) > ref) {
        ref = abs_q15_val(b);
    }
    tol = saturate_q15_i32((int32_t)config->tolerance_abs +
                           (int32_t)mul_q15(config->tolerance_rel, ref));
    if (diff > tol) {
        return BM_ALGO_FAULT_REDUNDANT_MISMATCH;
    }
    return 0u;
}

uint32_t bm_algo_redundant_pair_q31_step(
    bm_algo_q31_t a,
    bm_algo_q31_t b,
    const bm_algo_redundant_pair_q31_config_t *config) {
    bm_algo_q31_t diff;
    bm_algo_q31_t ref;
    bm_algo_q31_t tol;

    diff = abs_q31_val(a - b);
    if (config == NULL) {
        return (diff > 0) ? BM_ALGO_FAULT_REDUNDANT_MISMATCH : 0u;
    }

    ref = abs_q31_val(a);
    if (abs_q31_val(b) > ref) {
        ref = abs_q31_val(b);
    }
    tol = saturate_q31_i64((int64_t)config->tolerance_abs +
                           (int64_t)mul_q31(config->tolerance_rel, ref));
    if (diff > tol) {
        return BM_ALGO_FAULT_REDUNDANT_MISMATCH;
    }
    return 0u;
}

void bm_algo_rate_est_q15_reset(bm_algo_rate_est_q15_state_t *state,
                                bm_algo_q15_t input) {
    if (state == NULL) {
        return;
    }
    state->prev_input = input;
    state->rate_per_s = 0;
}

bm_algo_q15_t bm_algo_rate_est_q15_step(bm_algo_rate_est_q15_state_t *state,
                                        bm_algo_q15_t input,
                                        bm_algo_q15_t dt_q15) {
    if (state == NULL || dt_q15 <= 0) {
        return state != NULL ? state->rate_per_s : 0;
    }

    state->rate_per_s = div_q15((int32_t)input - (int32_t)state->prev_input,
                                dt_q15);
    state->prev_input = input;
    return state->rate_per_s;
}

bm_algo_q15_t bm_algo_soc_fusion_q15_step(
    bm_algo_q15_t soc_coulomb,
    bm_algo_q15_t soc_ocv,
    const bm_algo_soc_fusion_q15_config_t *config) {
    bm_algo_q15_t w;
    bm_algo_q15_t one_minus_w;
    int32_t fused;

    if (config == NULL) {
        return soc_coulomb;
    }

    w = bm_algo_clamp_q15(config->ocv_weight, 0, BM_ALGO_Q15_ONE);
    one_minus_w = BM_ALGO_Q15_ONE - w;
    fused = (int32_t)mul_q15(one_minus_w, soc_coulomb) +
            (int32_t)mul_q15(w, soc_ocv);
    return saturate_q15_i32(fused);
}

void bm_algo_scurve_q15_reset(bm_algo_scurve_q15_state_t *state,
                              bm_algo_q15_t position,
                              bm_algo_q15_t velocity,
                              bm_algo_q15_t acceleration) {
    if (state != NULL) {
        state->position = position;
        state->velocity = velocity;
        state->acceleration = acceleration;
        state->target = position;
        state->done = 1;
    }
}

void bm_algo_scurve_q15_set_target(bm_algo_scurve_q15_state_t *state,
                                   bm_algo_q15_t target) {
    if (state != NULL) {
        state->target = target;
        state->done = 0;
    }
}

bm_algo_q15_t bm_algo_scurve_q15_step(bm_algo_scurve_q15_state_t *state,
                                      const bm_algo_scurve_q15_config_t *config,
                                      bm_algo_q15_t dt_q15) {
    bm_algo_scurve_config_t fcfg;
    bm_algo_scurve_state_t fst;
    float pos;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return (state != NULL) ? state->position : 0;
    }

    fcfg.max_vel = bm_algo_q15_to_float(config->max_vel_q15);
    fcfg.max_accel = bm_algo_q15_to_float(config->max_accel_q15);
    fcfg.max_jerk = bm_algo_q15_to_float(config->max_jerk_q15);

    fst.position = bm_algo_q15_to_float(state->position);
    fst.velocity = bm_algo_q15_to_float(state->velocity);
    fst.acceleration = bm_algo_q15_to_float(state->acceleration);
    fst.target = bm_algo_q15_to_float(state->target);
    fst.done = state->done;

    pos = bm_algo_scurve_step(&fst, &fcfg, bm_algo_q15_to_float(dt_q15));

    state->position = bm_algo_float_to_q15(fst.position);
    state->velocity = bm_algo_float_to_q15(fst.velocity);
    state->acceleration = bm_algo_float_to_q15(fst.acceleration);
    state->done = fst.done;
    return bm_algo_float_to_q15(pos);
}

void bm_algo_rate_est_q31_reset(bm_algo_rate_est_q31_state_t *state,
                                bm_algo_q31_t input) {
    if (state == NULL) {
        return;
    }
    state->prev_input = input;
    state->rate_per_s = 0;
}

bm_algo_q31_t bm_algo_rate_est_q31_step(bm_algo_rate_est_q31_state_t *state,
                                        bm_algo_q31_t input,
                                        bm_algo_q31_t dt_q31) {
    if (state == NULL || dt_q31 <= 0) {
        return state != NULL ? state->rate_per_s : 0;
    }

    state->rate_per_s = div_q31((int64_t)input - (int64_t)state->prev_input,
                                dt_q31);
    state->prev_input = input;
    return state->rate_per_s;
}

bm_algo_q31_t bm_algo_soc_fusion_q31_step(
    bm_algo_q31_t soc_coulomb,
    bm_algo_q31_t soc_ocv,
    const bm_algo_soc_fusion_q31_config_t *config) {
    bm_algo_q31_t w;
    bm_algo_q31_t one_minus_w;
    int64_t fused;

    if (config == NULL) {
        return soc_coulomb;
    }

    w = bm_algo_clamp_q31(config->ocv_weight, 0, BM_ALGO_Q31_ONE);
    one_minus_w = BM_ALGO_Q31_ONE - w;
    fused = (int64_t)mul_q31(one_minus_w, soc_coulomb) +
            (int64_t)mul_q31(w, soc_ocv);
    return saturate_q31_i64(fused);
}

void bm_algo_mppt_po_q15_reset(bm_algo_mppt_po_q15_state_t *state,
                               bm_algo_q15_t v_init_q15) {
    if (state != NULL) {
        state->v_ref_q15 = v_init_q15;
        state->prev_power_q15 = 0;
        state->direction = 1;
    }
}

bm_algo_q15_t bm_algo_mppt_po_q15_step(bm_algo_mppt_po_q15_state_t *state,
                                     const bm_algo_mppt_po_q15_config_t *config,
                                     bm_algo_q15_t voltage_q15,
                                     bm_algo_q15_t current_q15) {
    bm_algo_q15_t power;

    if (state == NULL || config == NULL) {
        return voltage_q15;
    }

    power = mul_q15(voltage_q15, current_q15);
    if (power < state->prev_power_q15) {
        state->direction = -state->direction;
    }
    state->prev_power_q15 = power;

    state->v_ref_q15 = saturate_q15_i32(
        (int32_t)state->v_ref_q15 +
        (int32_t)state->direction * (int32_t)config->step_v_q15);
    state->v_ref_q15 = bm_algo_clamp_q15(state->v_ref_q15,
                                         config->v_min_q15,
                                         config->v_max_q15);
    return state->v_ref_q15;
}

void bm_algo_mppt_ic_q15_reset(bm_algo_mppt_ic_q15_state_t *state,
                               bm_algo_q15_t v_init_q15) {
    if (state != NULL) {
        state->v_ref_q15 = v_init_q15;
        state->prev_v_q15 = v_init_q15;
        state->prev_i_q15 = 0;
    }
}

bm_algo_q15_t bm_algo_mppt_ic_q15_step(bm_algo_mppt_ic_q15_state_t *state,
                                     const bm_algo_mppt_ic_q15_config_t *config,
                                     bm_algo_q15_t voltage_q15,
                                     bm_algo_q15_t current_q15) {
    bm_algo_q15_t dv;
    bm_algo_q15_t di;
    int64_t lhs;
    int64_t rhs;

    if (state == NULL || config == NULL) {
        return voltage_q15;
    }

    dv = voltage_q15 - state->prev_v_q15;
    di = current_q15 - state->prev_i_q15;

    if (dv != 0 && voltage_q15 != 0) {
        lhs = (int64_t)di * (int64_t)voltage_q15;
        rhs = -(int64_t)current_q15 * (int64_t)dv;
        if (lhs >= rhs) {
            state->v_ref_q15 = saturate_q15_i32(
                (int32_t)state->v_ref_q15 + (int32_t)config->step_v_q15);
        } else {
            state->v_ref_q15 = saturate_q15_i32(
                (int32_t)state->v_ref_q15 - (int32_t)config->step_v_q15);
        }
    }

    state->prev_v_q15 = voltage_q15;
    state->prev_i_q15 = current_q15;
    state->v_ref_q15 = bm_algo_clamp_q15(state->v_ref_q15,
                                         config->v_min_q15,
                                         config->v_max_q15);
    return state->v_ref_q15;
}

void bm_algo_range_monitor_q15_reset(bm_algo_range_monitor_q15_state_t *state,
                                     bm_algo_q15_t v_q15) {
    if (state != NULL) {
        state->prev_q15 = v_q15;
        state->fault_flags = 0u;
    }
}

uint32_t bm_algo_range_monitor_q15_step(
    bm_algo_range_monitor_q15_state_t *state,
    const bm_algo_range_monitor_q15_config_t *config,
    bm_algo_q15_t sample_q15,
    bm_algo_q15_t dt_q15) {
    bm_algo_q15_t rate;
    uint32_t flags = 0u;

    if (state == NULL || config == NULL) {
        return 0u;
    }

    if (sample_q15 < config->min_v_q15) {
        flags |= BM_ALGO_FAULT_UNDER_RANGE;
    }
    if (sample_q15 > config->max_v_q15) {
        flags |= BM_ALGO_FAULT_OVER_RANGE;
    }

    if (dt_q15 > 0) {
        rate = div_q15((int32_t)sample_q15 - (int32_t)state->prev_q15,
                       dt_q15);
        if (abs_q15_val(rate) > config->max_rate_per_s_q15) {
            flags |= BM_ALGO_FAULT_RATE;
        }
    }

    if (sample_q15 == state->prev_q15) {
        flags |= BM_ALGO_FAULT_FROZEN;
    }

    state->prev_q15 = sample_q15;
    state->fault_flags = flags;
    return flags;
}

void bm_algo_debounce_analog_q15_reset(bm_algo_debounce_analog_q15_state_t *state,
                                       bm_algo_q15_t initial_q15) {
    if (state != NULL) {
        state->candidate_q15 = initial_q15;
        state->stable_count = 0u;
        state->latched_q15 = initial_q15;
        state->valid = 1;
    }
}

int bm_algo_debounce_analog_q15_step(
    bm_algo_debounce_analog_q15_state_t *state,
    const bm_algo_debounce_analog_q15_config_t *config,
    bm_algo_q15_t sample_q15) {
    bm_algo_q15_t diff;

    if (state == NULL || config == NULL) {
        return 0;
    }

    diff = abs_q15_val(sample_q15 - state->candidate_q15);
    if (diff <= config->tolerance_q15) {
        state->stable_count++;
    } else {
        state->candidate_q15 = sample_q15;
        state->stable_count = 0u;
    }

    if (state->stable_count >= config->stable_count_required) {
        state->latched_q15 = state->candidate_q15;
        state->valid = 1;
        return 1;
    }

    return 0;
}

void bm_algo_energy_wh_q15_reset(bm_algo_energy_wh_q15_state_t *state) {
    if (state != NULL) {
        state->accumulated_wh_q31 = 0;
    }
}

bm_algo_q31_t bm_algo_energy_wh_integrator_q15_step(
    bm_algo_energy_wh_q15_state_t *state,
    bm_algo_q15_t p_q15,
    bm_algo_q15_t dt_q15) {
    int64_t prod;
    int64_t inc;

    if (state == NULL || dt_q15 <= 0) {
        return (state != NULL) ? state->accumulated_wh_q31 : 0;
    }

    /* prod = p·dt 为 Q30（Q15×Q15）。物理 Wh = P·dt/3600，累加值定标为 Q31：
     * (prod/2^30)/3600 · 2^31 = (prod<<1)/3600，与 Q31 版标度一致。 */
    prod = (int64_t)p_q15 * (int64_t)dt_q15;
    inc = (prod << 1) / 3600;
    state->accumulated_wh_q31 = saturate_q31_i64(
        (int64_t)state->accumulated_wh_q31 + inc);
    return state->accumulated_wh_q31;
}

/* ---------- 通用定点扩展（第十四批） ---------- */

static void median_sort_q15(bm_algo_q15_t *a, uint16_t n) {
    uint16_t i;
    uint16_t j;

    for (i = 1u; i < n; ++i) {
        bm_algo_q15_t t = a[i];

        for (j = i; j > 0u && a[j - 1u] > t; --j) {
            a[j] = a[j - 1u];
            a[j - 1u] = t;
        }
    }
}

static void median_sort_q31(bm_algo_q31_t *a, uint16_t n) {
    uint16_t i;
    uint16_t j;

    for (i = 1u; i < n; ++i) {
        bm_algo_q31_t t = a[i];

        for (j = i; j > 0u && a[j - 1u] > t; --j) {
            a[j] = a[j - 1u];
            a[j - 1u] = t;
        }
    }
}

static void dda_q15_sync_from_float(bm_algo_dda_q15_state_t *state,
                                    const bm_algo_dda_state_t *fs) {
    state->x = fs->x;
    state->y = fs->y;
    state->err = fs->err;
    state->step_x = fs->step_x;
    state->step_y = fs->step_y;
    state->dx = fs->dx;
    state->dy = fs->dy;
    state->target_x = fs->target_x;
    state->target_y = fs->target_y;
    state->step_size = fs->step_size;
    state->steps = fs->steps;
    state->step_count = fs->step_count;
    state->done = fs->done;
    state->x_q15 = bm_algo_float_to_q15(fs->x);
    state->y_q15 = bm_algo_float_to_q15(fs->y);
}

static void dda_q15_to_float_state(const bm_algo_dda_q15_state_t *state,
                                   bm_algo_dda_state_t *fs) {
    fs->x = state->x;
    fs->y = state->y;
    fs->err = state->err;
    fs->step_x = state->step_x;
    fs->step_y = state->step_y;
    fs->dx = state->dx;
    fs->dy = state->dy;
    fs->target_x = state->target_x;
    fs->target_y = state->target_y;
    fs->step_size = state->step_size;
    fs->steps = state->steps;
    fs->step_count = state->step_count;
    fs->done = state->done;
}

void bm_algo_complementary_q31_reset(bm_algo_complementary_q31_state_t *state) {
    if (state != NULL) {
        state->roll_rad = 0;
        state->pitch_rad = 0;
    }
}

void bm_algo_complementary_q31_step(bm_algo_complementary_q31_state_t *state,
                                    const bm_algo_complementary_q31_config_t *config,
                                    bm_algo_q31_t gx_q31,
                                    bm_algo_q31_t gy_q31,
                                    bm_algo_q31_t gz_q31,
                                    bm_algo_q31_t ax_q31,
                                    bm_algo_q31_t ay_q31,
                                    bm_algo_q31_t az_q31,
                                    bm_algo_q31_t dt_q31) {
    float roll_gyro;
    float pitch_gyro;
    float roll_acc;
    float pitch_acc;
    float alpha;
    float dt_s;

    (void)gz_q31;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return;
    }

    dt_s = bm_algo_q31_to_float(dt_q31);
    roll_gyro = bm_algo_q31_to_float(state->roll_rad) +
                bm_algo_q31_to_float(gx_q31) * dt_s;
    pitch_gyro = bm_algo_q31_to_float(state->pitch_rad) +
                 bm_algo_q31_to_float(gy_q31) * dt_s;

    roll_acc = atan2f(bm_algo_q31_to_float(ay_q31), bm_algo_q31_to_float(az_q31));
    pitch_acc = atan2f(-bm_algo_q31_to_float(ax_q31),
                       sqrtf(bm_algo_q31_to_float(ay_q31) *
                                 bm_algo_q31_to_float(ay_q31) +
                             bm_algo_q31_to_float(az_q31) *
                                 bm_algo_q31_to_float(az_q31)));

    alpha = bm_algo_q31_to_float(
        bm_algo_clamp_q31(config->alpha_q31, 0, BM_ALGO_Q31_ONE));
    state->roll_rad = bm_algo_float_to_q31(
        alpha * roll_gyro + (1.0f - alpha) * roll_acc);
    state->pitch_rad = bm_algo_float_to_q31(
        alpha * pitch_gyro + (1.0f - alpha) * pitch_acc);
}

void bm_algo_dda_q15_reset(bm_algo_dda_q15_state_t *state,
                          const bm_algo_dda_q15_config_t *config) {
    bm_algo_dda_config_t fc;
    bm_algo_dda_state_t fs;

    if (state == NULL || config == NULL) {
        return;
    }

    fc.x0 = bm_algo_q15_to_float(config->x0_q15);
    fc.y0 = bm_algo_q15_to_float(config->y0_q15);
    fc.x1 = bm_algo_q15_to_float(config->x1_q15);
    fc.y1 = bm_algo_q15_to_float(config->y1_q15);
    fc.step_size = bm_algo_q15_to_float(config->step_size_q15);
    bm_algo_dda_reset(&fs, &fc);
    dda_q15_sync_from_float(state, &fs);
}

int bm_algo_dda_q15_step(bm_algo_dda_q15_state_t *state,
                         const bm_algo_dda_q15_config_t *config,
                         bm_algo_q15_t *x_out_q15,
                         bm_algo_q15_t *y_out_q15) {
    bm_algo_dda_config_t fc;
    bm_algo_dda_state_t fs;
    float x_out;
    float y_out;
    int ok;

    if (state == NULL || config == NULL) {
        return 0;
    }

    fc.x0 = bm_algo_q15_to_float(config->x0_q15);
    fc.y0 = bm_algo_q15_to_float(config->y0_q15);
    fc.x1 = bm_algo_q15_to_float(config->x1_q15);
    fc.y1 = bm_algo_q15_to_float(config->y1_q15);
    fc.step_size = bm_algo_q15_to_float(config->step_size_q15);
    dda_q15_to_float_state(state, &fs);
    ok = bm_algo_dda_step(&fs, &fc, &x_out, &y_out);
    dda_q15_sync_from_float(state, &fs);
    if (x_out_q15 != NULL) {
        *x_out_q15 = state->x_q15;
    }
    if (y_out_q15 != NULL) {
        *y_out_q15 = state->y_q15;
    }
    return ok;
}

static void dda_q31_sync_from_float(bm_algo_dda_q31_state_t *state,
                                    const bm_algo_dda_state_t *fs) {
    state->x = fs->x;
    state->y = fs->y;
    state->err = fs->err;
    state->step_x = fs->step_x;
    state->step_y = fs->step_y;
    state->dx = fs->dx;
    state->dy = fs->dy;
    state->target_x = fs->target_x;
    state->target_y = fs->target_y;
    state->step_size = fs->step_size;
    state->steps = fs->steps;
    state->step_count = fs->step_count;
    state->done = fs->done;
    state->x_q31 = bm_algo_float_to_q31(fs->x);
    state->y_q31 = bm_algo_float_to_q31(fs->y);
}

static void dda_q31_to_float_state(const bm_algo_dda_q31_state_t *state,
                                   bm_algo_dda_state_t *fs) {
    fs->x = state->x;
    fs->y = state->y;
    fs->err = state->err;
    fs->step_x = state->step_x;
    fs->step_y = state->step_y;
    fs->dx = state->dx;
    fs->dy = state->dy;
    fs->target_x = state->target_x;
    fs->target_y = state->target_y;
    fs->step_size = state->step_size;
    fs->steps = state->steps;
    fs->step_count = state->step_count;
    fs->done = state->done;
}

void bm_algo_dda_q31_reset(bm_algo_dda_q31_state_t *state,
                          const bm_algo_dda_q31_config_t *config) {
    bm_algo_dda_config_t fc;
    bm_algo_dda_state_t fs;

    if (state == NULL || config == NULL) {
        return;
    }

    fc.x0 = bm_algo_q31_to_float(config->x0_q31);
    fc.y0 = bm_algo_q31_to_float(config->y0_q31);
    fc.x1 = bm_algo_q31_to_float(config->x1_q31);
    fc.y1 = bm_algo_q31_to_float(config->y1_q31);
    fc.step_size = bm_algo_q31_to_float(config->step_size_q31);
    bm_algo_dda_reset(&fs, &fc);
    dda_q31_sync_from_float(state, &fs);
}

int bm_algo_dda_q31_step(bm_algo_dda_q31_state_t *state,
                         const bm_algo_dda_q31_config_t *config,
                         bm_algo_q31_t *x_out_q31,
                         bm_algo_q31_t *y_out_q31) {
    bm_algo_dda_config_t fc;
    bm_algo_dda_state_t fs;
    float x_out;
    float y_out;
    int ok;

    if (state == NULL || config == NULL) {
        return 0;
    }

    fc.x0 = bm_algo_q31_to_float(config->x0_q31);
    fc.y0 = bm_algo_q31_to_float(config->y0_q31);
    fc.x1 = bm_algo_q31_to_float(config->x1_q31);
    fc.y1 = bm_algo_q31_to_float(config->y1_q31);
    fc.step_size = bm_algo_q31_to_float(config->step_size_q31);
    dda_q31_to_float_state(state, &fs);
    ok = bm_algo_dda_step(&fs, &fc, &x_out, &y_out);
    dda_q31_sync_from_float(state, &fs);
    if (x_out_q31 != NULL) {
        *x_out_q31 = state->x_q31;
    }
    if (y_out_q31 != NULL) {
        *y_out_q31 = state->y_q31;
    }
    return ok;
}

void bm_algo_debounce_analog_q31_reset(bm_algo_debounce_analog_q31_state_t *state,
                                       bm_algo_q31_t initial_q31) {
    if (state != NULL) {
        state->candidate_q31 = initial_q31;
        state->stable_count = 0u;
        state->latched_q31 = initial_q31;
        state->valid = 1;
    }
}

int bm_algo_debounce_analog_q31_step(
    bm_algo_debounce_analog_q31_state_t *state,
    const bm_algo_debounce_analog_q31_config_t *config,
    bm_algo_q31_t sample_q31) {
    bm_algo_q31_t diff;

    if (state == NULL || config == NULL) {
        return 0;
    }

    diff = abs_q31_val(sample_q31 - state->candidate_q31);
    if (diff <= config->tolerance_q31) {
        state->stable_count++;
    } else {
        state->candidate_q31 = sample_q31;
        state->stable_count = 0u;
    }

    if (state->stable_count >= config->stable_count_required) {
        state->latched_q31 = state->candidate_q31;
        state->valid = 1;
        return 1;
    }

    return 0;
}

void bm_algo_decimator_q15_reset(bm_algo_decimator_q15_state_t *state) {
    if (state != NULL) {
        state->counter = 0u;
        state->decim = 1u;
    }
}

int bm_algo_decimator_q15_step(bm_algo_decimator_q15_state_t *state,
                               uint32_t decim,
                               bm_algo_q15_t input_q15,
                               bm_algo_q15_t *output_q15) {
    if (state == NULL || decim == 0u) {
        return 0;
    }

    state->decim = decim;
    if (state->counter == 0u) {
        if (output_q15 != NULL) {
            *output_q15 = input_q15;
        }
        state->counter = decim - 1u;
        return 1;
    }

    if (state->counter > 0u) {
        state->counter--;
    }
    return 0;
}

void bm_algo_decimator_q31_reset(bm_algo_decimator_q31_state_t *state) {
    if (state != NULL) {
        state->counter = 0u;
        state->decim = 1u;
    }
}

int bm_algo_decimator_q31_step(bm_algo_decimator_q31_state_t *state,
                               uint32_t decim,
                               bm_algo_q31_t input_q31,
                               bm_algo_q31_t *output_q31) {
    if (state == NULL || decim == 0u) {
        return 0;
    }

    state->decim = decim;
    if (state->counter == 0u) {
        if (output_q31 != NULL) {
            *output_q31 = input_q31;
        }
        state->counter = decim - 1u;
        return 1;
    }

    if (state->counter > 0u) {
        state->counter--;
    }
    return 0;
}

void bm_algo_encoder_diag_q15_reset(bm_algo_encoder_diag_q15_state_t *state,
                                    int32_t raw_count) {
    if (state != NULL) {
        state->prev_count = raw_count;
    }
}

uint32_t bm_algo_encoder_diag_q15_step(
    bm_algo_encoder_diag_q15_state_t *state,
    const bm_algo_encoder_diag_q15_config_t *config,
    int32_t raw_count,
    int index_pulse_seen) {
    bm_algo_encoder_diag_state_t fst;
    bm_algo_encoder_diag_config_t fcfg;

    if (state == NULL || config == NULL) {
        return BM_ALGO_ENCODER_FAULT_NONE;
    }

    fcfg.max_delta_per_step = config->max_delta_per_step;
    fst.prev_count = state->prev_count;
    {
        uint32_t faults = bm_algo_encoder_diag_step(
            &fst, &fcfg, raw_count, index_pulse_seen);

        state->prev_count = fst.prev_count;
        return faults;
    }
}

void bm_algo_encoder_diag_q31_reset(bm_algo_encoder_diag_q31_state_t *state,
                                    int32_t raw_count) {
    if (state != NULL) {
        state->prev_count = raw_count;
    }
}

uint32_t bm_algo_encoder_diag_q31_step(
    bm_algo_encoder_diag_q31_state_t *state,
    const bm_algo_encoder_diag_q31_config_t *config,
    int32_t raw_count,
    int index_pulse_seen) {
    bm_algo_encoder_diag_state_t fst;
    bm_algo_encoder_diag_config_t fcfg;

    if (state == NULL || config == NULL) {
        return BM_ALGO_ENCODER_FAULT_NONE;
    }

    fcfg.max_delta_per_step = config->max_delta_per_step;
    fst.prev_count = state->prev_count;
    {
        uint32_t faults = bm_algo_encoder_diag_step(
            &fst, &fcfg, raw_count, index_pulse_seen);

        state->prev_count = fst.prev_count;
        return faults;
    }
}

void bm_algo_energy_wh_q31_reset(bm_algo_energy_wh_q31_state_t *state) {
    if (state != NULL) {
        state->accumulated_wh_q31 = 0;
    }
}

bm_algo_q31_t bm_algo_energy_wh_integrator_q31_step(
    bm_algo_energy_wh_q31_state_t *state,
    bm_algo_q31_t p_q31,
    bm_algo_q31_t dt_q31) {
    int64_t prod;
    int64_t inc;

    if (state == NULL || dt_q31 <= 0) {
        return (state != NULL) ? state->accumulated_wh_q31 : 0;
    }

    prod = (int64_t)mul_q31(p_q31, dt_q31);
    inc = prod / 3600;
    state->accumulated_wh_q31 = saturate_q31_i64(
        (int64_t)state->accumulated_wh_q31 + inc);
    return state->accumulated_wh_q31;
}

int bm_algo_fir_q15_init(bm_algo_fir_q15_state_t *state,
                         const bm_algo_fir_q15_config_t *config) {
    if (state == NULL || config == NULL ||
        config->coeffs == NULL || config->delay_line == NULL ||
        config->tap_count == 0u ||
        config->tap_count > BM_ALGO_FIR_Q15_MAX_TAPS) {
        return -1;
    }
    bm_algo_fir_q15_reset(state, config);
    return 0;
}

void bm_algo_fir_q15_reset(bm_algo_fir_q15_state_t *state,
                           const bm_algo_fir_q15_config_t *config) {
    if (state == NULL || config == NULL || config->delay_line == NULL) {
        return;
    }
    state->index = 0u;
    state->tap_count = config->tap_count;
    memset(config->delay_line, 0, config->tap_count * sizeof(bm_algo_q15_t));
}

bm_algo_q15_t bm_algo_fir_q15_step(bm_algo_fir_q15_state_t *state,
                                   const bm_algo_fir_q15_config_t *config,
                                   bm_algo_q15_t input_q15) {
    int32_t acc = 0;
    uint8_t i;
    uint8_t idx;

    if (state == NULL || config == NULL ||
        config->coeffs == NULL || config->delay_line == NULL ||
        config->tap_count == 0u ||
        config->tap_count != state->tap_count ||
        state->index >= state->tap_count) {
        return input_q15;
    }

    config->delay_line[state->index] = input_q15;
    for (i = 0u; i < config->tap_count; ++i) {
        idx = (uint8_t)((state->index + config->tap_count - i) %
                        config->tap_count);
        acc += (int32_t)mul_q15(config->coeffs[i], config->delay_line[idx]);
    }
    state->index = (uint8_t)((state->index + 1u) % config->tap_count);
    return saturate_q15_i32(acc);
}

int bm_algo_fir_q31_init(bm_algo_fir_q31_state_t *state,
                         const bm_algo_fir_q31_config_t *config) {
    if (state == NULL || config == NULL ||
        config->coeffs == NULL || config->delay_line == NULL ||
        config->tap_count == 0u ||
        config->tap_count > BM_ALGO_FIR_Q31_MAX_TAPS) {
        return -1;
    }
    bm_algo_fir_q31_reset(state, config);
    return 0;
}

void bm_algo_fir_q31_reset(bm_algo_fir_q31_state_t *state,
                           const bm_algo_fir_q31_config_t *config) {
    if (state == NULL || config == NULL || config->delay_line == NULL) {
        return;
    }
    state->index = 0u;
    state->tap_count = config->tap_count;
    memset(config->delay_line, 0, config->tap_count * sizeof(bm_algo_q31_t));
}

bm_algo_q31_t bm_algo_fir_q31_step(bm_algo_fir_q31_state_t *state,
                                   const bm_algo_fir_q31_config_t *config,
                                   bm_algo_q31_t input_q31) {
    int64_t acc = 0;
    uint8_t i;
    uint8_t idx;

    if (state == NULL || config == NULL ||
        config->coeffs == NULL || config->delay_line == NULL ||
        config->tap_count == 0u ||
        config->tap_count != state->tap_count ||
        state->index >= state->tap_count) {
        return input_q31;
    }

    config->delay_line[state->index] = input_q31;
    for (i = 0u; i < config->tap_count; ++i) {
        idx = (uint8_t)((state->index + config->tap_count - i) %
                        config->tap_count);
        acc += (int64_t)mul_q31(config->coeffs[i], config->delay_line[idx]);
    }
    state->index = (uint8_t)((state->index + 1u) % config->tap_count);
    return saturate_q31_i64(acc);
}

void bm_algo_flux_observer_q15_reset(bm_algo_flux_observer_q15_state_t *state,
                                     bm_algo_q15_t theta_rad_q15) {
    if (state != NULL) {
        state->theta_rad = bm_algo_q15_to_float(theta_rad_q15);
        state->omega_rad_s = 0.0f;
        state->flux_alpha = 0.0f;
        state->flux_beta = 0.0f;
        state->theta_rad_q15 = theta_rad_q15;
        state->omega_rad_s_q15 = 0;
    }
}

bm_algo_q15_t bm_algo_flux_observer_q15_step(
    bm_algo_flux_observer_q15_state_t *state,
    const bm_algo_flux_observer_q15_config_t *config,
    bm_algo_q15_t v_alpha_q15,
    bm_algo_q15_t v_beta_q15,
    bm_algo_q15_t i_alpha_q15,
    bm_algo_q15_t i_beta_q15,
    bm_algo_q15_t dt_q15) {
    bm_algo_flux_observer_config_t fcfg;
    bm_algo_flux_observer_state_t fst;
    float theta;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return 0;
    }

    fcfg.rs_ohm = bm_algo_q15_to_float(config->rs_q15);
    fcfg.ls_h = bm_algo_q15_to_float(config->ls_q15);
    fcfg.pll_kp = bm_algo_q15_to_float(config->pll_kp_q15);
    fcfg.pll_ki = bm_algo_q15_to_float(config->pll_ki_q15);
    fcfg.flux_observer_wc_rad_s = config->wc_rad_s; /* 衰减截止频率；0.0f 时退化为纯积分 */
    fst.theta_rad = state->theta_rad;
    fst.omega_rad_s = state->omega_rad_s;
    fst.flux_alpha = state->flux_alpha;
    fst.flux_beta = state->flux_beta;
    theta = bm_algo_flux_observer_step(
        &fst, &fcfg,
        bm_algo_q15_to_float(v_alpha_q15),
        bm_algo_q15_to_float(v_beta_q15),
        bm_algo_q15_to_float(i_alpha_q15),
        bm_algo_q15_to_float(i_beta_q15),
        bm_algo_q15_to_float(dt_q15));
    state->theta_rad = fst.theta_rad;
    state->omega_rad_s = fst.omega_rad_s;
    state->flux_alpha = fst.flux_alpha;
    state->flux_beta = fst.flux_beta;
    state->theta_rad_q15 = bm_algo_float_to_q15(theta);
    state->omega_rad_s_q15 = bm_algo_float_to_q15(fst.omega_rad_s);
    return state->theta_rad_q15;
}

void bm_algo_flux_observer_q31_reset(bm_algo_flux_observer_q31_state_t *state,
                                     bm_algo_q31_t theta_rad_q31) {
    if (state != NULL) {
        state->theta_rad = bm_algo_q31_to_float(theta_rad_q31);
        state->omega_rad_s = 0.0f;
        state->flux_alpha = 0.0f;
        state->flux_beta = 0.0f;
        state->theta_rad_q31 = theta_rad_q31;
        state->omega_rad_s_q31 = 0;
    }
}

bm_algo_q31_t bm_algo_flux_observer_q31_step(
    bm_algo_flux_observer_q31_state_t *state,
    const bm_algo_flux_observer_q31_config_t *config,
    bm_algo_q31_t v_alpha_q31,
    bm_algo_q31_t v_beta_q31,
    bm_algo_q31_t i_alpha_q31,
    bm_algo_q31_t i_beta_q31,
    bm_algo_q31_t dt_q31) {
    bm_algo_flux_observer_config_t fcfg;
    bm_algo_flux_observer_state_t fst;
    float theta;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return 0;
    }

    fcfg.rs_ohm = bm_algo_q31_to_float(config->rs_q31);
    fcfg.ls_h = bm_algo_q31_to_float(config->ls_q31);
    fcfg.pll_kp = bm_algo_q31_to_float(config->pll_kp_q31);
    fcfg.pll_ki = bm_algo_q31_to_float(config->pll_ki_q31);
    fcfg.flux_observer_wc_rad_s = config->wc_rad_s; /* 衰减截止频率；0.0f 时退化为纯积分 */
    fst.theta_rad = state->theta_rad;
    fst.omega_rad_s = state->omega_rad_s;
    fst.flux_alpha = state->flux_alpha;
    fst.flux_beta = state->flux_beta;
    theta = bm_algo_flux_observer_step(
        &fst, &fcfg,
        bm_algo_q31_to_float(v_alpha_q31),
        bm_algo_q31_to_float(v_beta_q31),
        bm_algo_q31_to_float(i_alpha_q31),
        bm_algo_q31_to_float(i_beta_q31),
        bm_algo_q31_to_float(dt_q31));
    state->theta_rad = fst.theta_rad;
    state->omega_rad_s = fst.omega_rad_s;
    state->flux_alpha = fst.flux_alpha;
    state->flux_beta = fst.flux_beta;
    state->theta_rad_q31 = bm_algo_float_to_q31(theta);
    state->omega_rad_s_q31 = bm_algo_float_to_q31(fst.omega_rad_s);
    return state->theta_rad_q31;
}

void bm_algo_linear_resampler_q15_reset(
    bm_algo_linear_resampler_q15_state_t *state,
    bm_algo_q15_t ratio_q15,
    bm_algo_q15_t initial_q15) {
    bm_algo_linear_resampler_state_t fst;
    float ratio;

    if (state == NULL) {
        return;
    }

    ratio = bm_algo_q15_to_float(ratio_q15);
    bm_algo_linear_resampler_reset(&fst, ratio, bm_algo_q15_to_float(initial_q15));
    state->ratio_q15 = ratio_q15;
    state->phase_q15 = bm_algo_float_to_q15(fst.phase);
    state->prev_sample_q15 = initial_q15;
}

int bm_algo_linear_resampler_q15_step(
    bm_algo_linear_resampler_q15_state_t *state,
    bm_algo_q15_t input_q15,
    bm_algo_q15_t *outputs_q15,
    uint32_t max_outputs,
    uint32_t *out_count) {
    bm_algo_linear_resampler_state_t fst;
    float outputs_f[8];
    float ratio;
    uint32_t n;
    uint32_t i;
    int rc;

    if (state == NULL || outputs_q15 == NULL || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0u;
        }
        return 0;
    }

    ratio = bm_algo_q15_to_float(state->ratio_q15);
    fst.ratio = ratio;
    fst.phase = bm_algo_q15_to_float(state->phase_q15);
    fst.prev_sample = bm_algo_q15_to_float(state->prev_sample_q15);
    if (max_outputs > 8u) {
        max_outputs = 8u;
    }
    rc = bm_algo_linear_resampler_step(
        &fst, bm_algo_q15_to_float(input_q15),
        outputs_f, max_outputs, &n);
    state->phase_q15 = bm_algo_float_to_q15(fst.phase);
    state->prev_sample_q15 = input_q15;
    *out_count = n;
    for (i = 0u; i < n; ++i) {
        outputs_q15[i] = bm_algo_float_to_q15(outputs_f[i]);
    }
    return rc;
}

void bm_algo_linear_resampler_q31_reset(
    bm_algo_linear_resampler_q31_state_t *state,
    bm_algo_q31_t ratio_q31,
    bm_algo_q31_t initial_q31) {
    bm_algo_linear_resampler_state_t fst;
    float ratio;

    if (state == NULL) {
        return;
    }

    ratio = bm_algo_q31_to_float(ratio_q31);
    bm_algo_linear_resampler_reset(&fst, ratio, bm_algo_q31_to_float(initial_q31));
    state->ratio_q31 = ratio_q31;
    state->phase_q31 = bm_algo_float_to_q31(fst.phase);
    state->prev_sample_q31 = initial_q31;
}

int bm_algo_linear_resampler_q31_step(
    bm_algo_linear_resampler_q31_state_t *state,
    bm_algo_q31_t input_q31,
    bm_algo_q31_t *outputs_q31,
    uint32_t max_outputs,
    uint32_t *out_count) {
    bm_algo_linear_resampler_state_t fst;
    float outputs_f[8];
    float ratio;
    uint32_t n;
    uint32_t i;
    int rc;

    if (state == NULL || outputs_q31 == NULL || out_count == NULL) {
        if (out_count != NULL) {
            *out_count = 0u;
        }
        return 0;
    }

    ratio = bm_algo_q31_to_float(state->ratio_q31);
    fst.ratio = ratio;
    fst.phase = bm_algo_q31_to_float(state->phase_q31);
    fst.prev_sample = bm_algo_q31_to_float(state->prev_sample_q31);
    if (max_outputs > 8u) {
        max_outputs = 8u;
    }
    rc = bm_algo_linear_resampler_step(
        &fst, bm_algo_q31_to_float(input_q31),
        outputs_f, max_outputs, &n);
    state->phase_q31 = bm_algo_float_to_q31(fst.phase);
    state->prev_sample_q31 = input_q31;
    *out_count = n;
    for (i = 0u; i < n; ++i) {
        outputs_q31[i] = bm_algo_float_to_q31(outputs_f[i]);
    }
    return rc;
}

void bm_algo_madgwick_q15_reset(bm_algo_madgwick_q15_state_t *state) {
    if (state != NULL) {
        state->qw_q15 = BM_ALGO_Q15_ONE;
        state->qx_q15 = 0;
        state->qy_q15 = 0;
        state->qz_q15 = 0;
    }
}

void bm_algo_madgwick_q15_step(bm_algo_madgwick_q15_state_t *state,
                               const bm_algo_madgwick_q15_config_t *config,
                               bm_algo_q15_t gx_q15,
                               bm_algo_q15_t gy_q15,
                               bm_algo_q15_t gz_q15,
                               bm_algo_q15_t ax_q15,
                               bm_algo_q15_t ay_q15,
                               bm_algo_q15_t az_q15,
                               bm_algo_q15_t dt_q15) {
    bm_algo_madgwick_config_t fcfg;
    bm_algo_madgwick_state_t fst;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return;
    }

    fcfg.beta = bm_algo_q15_to_float(config->beta_q15);
    fst.q.w = bm_algo_q15_to_float(state->qw_q15);
    fst.q.x = bm_algo_q15_to_float(state->qx_q15);
    fst.q.y = bm_algo_q15_to_float(state->qy_q15);
    fst.q.z = bm_algo_q15_to_float(state->qz_q15);
    bm_algo_madgwick_step(
        &fst, &fcfg,
        bm_algo_q15_to_float(gx_q15),
        bm_algo_q15_to_float(gy_q15),
        bm_algo_q15_to_float(gz_q15),
        bm_algo_q15_to_float(ax_q15),
        bm_algo_q15_to_float(ay_q15),
        bm_algo_q15_to_float(az_q15),
        bm_algo_q15_to_float(dt_q15));
    state->qw_q15 = bm_algo_float_to_q15(fst.q.w);
    state->qx_q15 = bm_algo_float_to_q15(fst.q.x);
    state->qy_q15 = bm_algo_float_to_q15(fst.q.y);
    state->qz_q15 = bm_algo_float_to_q15(fst.q.z);
}

void bm_algo_madgwick_q31_reset(bm_algo_madgwick_q31_state_t *state) {
    if (state != NULL) {
        state->qw_q31 = BM_ALGO_Q31_ONE;
        state->qx_q31 = 0;
        state->qy_q31 = 0;
        state->qz_q31 = 0;
    }
}

void bm_algo_madgwick_q31_step(bm_algo_madgwick_q31_state_t *state,
                               const bm_algo_madgwick_q31_config_t *config,
                               bm_algo_q31_t gx_q31,
                               bm_algo_q31_t gy_q31,
                               bm_algo_q31_t gz_q31,
                               bm_algo_q31_t ax_q31,
                               bm_algo_q31_t ay_q31,
                               bm_algo_q31_t az_q31,
                               bm_algo_q31_t dt_q31) {
    bm_algo_madgwick_config_t fcfg;
    bm_algo_madgwick_state_t fst;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return;
    }

    fcfg.beta = bm_algo_q31_to_float(config->beta_q31);
    fst.q.w = bm_algo_q31_to_float(state->qw_q31);
    fst.q.x = bm_algo_q31_to_float(state->qx_q31);
    fst.q.y = bm_algo_q31_to_float(state->qy_q31);
    fst.q.z = bm_algo_q31_to_float(state->qz_q31);
    bm_algo_madgwick_step(
        &fst, &fcfg,
        bm_algo_q31_to_float(gx_q31),
        bm_algo_q31_to_float(gy_q31),
        bm_algo_q31_to_float(gz_q31),
        bm_algo_q31_to_float(ax_q31),
        bm_algo_q31_to_float(ay_q31),
        bm_algo_q31_to_float(az_q31),
        bm_algo_q31_to_float(dt_q31));
    state->qw_q31 = bm_algo_float_to_q31(fst.q.w);
    state->qx_q31 = bm_algo_float_to_q31(fst.q.x);
    state->qy_q31 = bm_algo_float_to_q31(fst.q.y);
    state->qz_q31 = bm_algo_float_to_q31(fst.q.z);
}

void bm_algo_mahony_q15_reset(bm_algo_mahony_q15_state_t *state) {
    if (state != NULL) {
        state->qw_q15    = BM_ALGO_Q15_ONE;
        state->qx_q15    = 0;
        state->qy_q15    = 0;
        state->qz_q15    = 0;
        state->integral_x = 0.0f;
        state->integral_y = 0.0f;
        state->integral_z = 0.0f;
    }
}

void bm_algo_mahony_q15_step(bm_algo_mahony_q15_state_t *state,
                             const bm_algo_mahony_q15_config_t *config,
                             bm_algo_q15_t gx_q15,
                             bm_algo_q15_t gy_q15,
                             bm_algo_q15_t gz_q15,
                             bm_algo_q15_t ax_q15,
                             bm_algo_q15_t ay_q15,
                             bm_algo_q15_t az_q15,
                             bm_algo_q15_t dt_q15) {
    bm_algo_mahony_config_t fcfg;
    bm_algo_mahony_state_t fst;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return;
    }

    fcfg.kp = bm_algo_q15_to_float(config->kp_q15);
    fcfg.ki = bm_algo_q15_to_float(config->ki_q15);
    fst.q.w = bm_algo_q15_to_float(state->qw_q15);
    fst.q.x = bm_algo_q15_to_float(state->qx_q15);
    fst.q.y = bm_algo_q15_to_float(state->qy_q15);
    fst.q.z = bm_algo_q15_to_float(state->qz_q15);
    /* 恢复帧间 Ki 积分项，确保积分效果跨帧持久 */
    fst.integral_x = state->integral_x;
    fst.integral_y = state->integral_y;
    fst.integral_z = state->integral_z;
    bm_algo_mahony_step(
        &fst, &fcfg,
        bm_algo_q15_to_float(gx_q15),
        bm_algo_q15_to_float(gy_q15),
        bm_algo_q15_to_float(gz_q15),
        bm_algo_q15_to_float(ax_q15),
        bm_algo_q15_to_float(ay_q15),
        bm_algo_q15_to_float(az_q15),
        bm_algo_q15_to_float(dt_q15));
    state->qw_q15    = bm_algo_float_to_q15(fst.q.w);
    state->qx_q15    = bm_algo_float_to_q15(fst.q.x);
    state->qy_q15    = bm_algo_float_to_q15(fst.q.y);
    state->qz_q15    = bm_algo_float_to_q15(fst.q.z);
    /* 回写更新后的积分项，供下一帧使用 */
    state->integral_x = fst.integral_x;
    state->integral_y = fst.integral_y;
    state->integral_z = fst.integral_z;
}

void bm_algo_mahony_q31_reset(bm_algo_mahony_q31_state_t *state) {
    if (state != NULL) {
        state->qw_q31    = BM_ALGO_Q31_ONE;
        state->qx_q31    = 0;
        state->qy_q31    = 0;
        state->qz_q31    = 0;
        state->integral_x = 0.0f;
        state->integral_y = 0.0f;
        state->integral_z = 0.0f;
    }
}

void bm_algo_mahony_q31_step(bm_algo_mahony_q31_state_t *state,
                             const bm_algo_mahony_q31_config_t *config,
                             bm_algo_q31_t gx_q31,
                             bm_algo_q31_t gy_q31,
                             bm_algo_q31_t gz_q31,
                             bm_algo_q31_t ax_q31,
                             bm_algo_q31_t ay_q31,
                             bm_algo_q31_t az_q31,
                             bm_algo_q31_t dt_q31) {
    bm_algo_mahony_config_t fcfg;
    bm_algo_mahony_state_t fst;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return;
    }

    fcfg.kp = bm_algo_q31_to_float(config->kp_q31);
    fcfg.ki = bm_algo_q31_to_float(config->ki_q31);
    fst.q.w = bm_algo_q31_to_float(state->qw_q31);
    fst.q.x = bm_algo_q31_to_float(state->qx_q31);
    fst.q.y = bm_algo_q31_to_float(state->qy_q31);
    fst.q.z = bm_algo_q31_to_float(state->qz_q31);
    /* 恢复帧间 Ki 积分项，确保积分效果跨帧持久 */
    fst.integral_x = state->integral_x;
    fst.integral_y = state->integral_y;
    fst.integral_z = state->integral_z;
    bm_algo_mahony_step(
        &fst, &fcfg,
        bm_algo_q31_to_float(gx_q31),
        bm_algo_q31_to_float(gy_q31),
        bm_algo_q31_to_float(gz_q31),
        bm_algo_q31_to_float(ax_q31),
        bm_algo_q31_to_float(ay_q31),
        bm_algo_q31_to_float(az_q31),
        bm_algo_q31_to_float(dt_q31));
    state->qw_q31    = bm_algo_float_to_q31(fst.q.w);
    state->qx_q31    = bm_algo_float_to_q31(fst.q.x);
    state->qy_q31    = bm_algo_float_to_q31(fst.q.y);
    state->qz_q31    = bm_algo_float_to_q31(fst.q.z);
    /* 回写更新后的积分项，供下一帧使用 */
    state->integral_x = fst.integral_x;
    state->integral_y = fst.integral_y;
    state->integral_z = fst.integral_z;
}

void bm_algo_median_q15_reset(bm_algo_median_q15_state_t *state) {
    if (state != NULL) {
        state->count = 0u;
        state->index = 0u;
    }
}

bm_algo_q15_t bm_algo_median_q15_step(bm_algo_median_q15_state_t *state,
                                      const bm_algo_median_q15_config_t *config,
                                      bm_algo_q15_t input_q15) {
    bm_algo_q15_t sorted[BM_ALGO_MEDIAN_Q15_MAX];
    uint16_t len;
    uint16_t i;

    if (state == NULL || config == NULL) {
        return input_q15;
    }

    len = config->window_size;
    if (len < 3u || len > BM_ALGO_MEDIAN_Q15_MAX || (len & 1u) != 0u) {
        return input_q15;
    }

    state->samples[state->index] = input_q15;
    state->index = (uint16_t)((state->index + 1u) % len);
    if (state->count < len) {
        state->count++;
    }

    for (i = 0u; i < state->count; ++i) {
        sorted[i] = state->samples[i];
    }
    median_sort_q15(sorted, state->count);
    return sorted[state->count / 2u];
}

void bm_algo_median_q31_reset(bm_algo_median_q31_state_t *state) {
    if (state != NULL) {
        state->count = 0u;
        state->index = 0u;
    }
}

bm_algo_q31_t bm_algo_median_q31_step(bm_algo_median_q31_state_t *state,
                                      const bm_algo_median_q31_config_t *config,
                                      bm_algo_q31_t input_q31) {
    bm_algo_q31_t sorted[BM_ALGO_MEDIAN_Q31_MAX];
    uint16_t len;
    uint16_t i;

    if (state == NULL || config == NULL) {
        return input_q31;
    }

    len = config->window_size;
    if (len < 3u || len > BM_ALGO_MEDIAN_Q31_MAX || (len & 1u) != 0u) {
        return input_q31;
    }

    state->samples[state->index] = input_q31;
    state->index = (uint16_t)((state->index + 1u) % len);
    if (state->count < len) {
        state->count++;
    }

    for (i = 0u; i < state->count; ++i) {
        sorted[i] = state->samples[i];
    }
    median_sort_q31(sorted, state->count);
    return sorted[state->count / 2u];
}

void bm_algo_mppt_po_q31_reset(bm_algo_mppt_po_q31_state_t *state,
                               bm_algo_q31_t v_init_q31) {
    if (state != NULL) {
        state->v_ref_q31 = v_init_q31;
        state->prev_power_q31 = 0;
        state->direction = 1;
    }
}

bm_algo_q31_t bm_algo_mppt_po_q31_step(bm_algo_mppt_po_q31_state_t *state,
                                       const bm_algo_mppt_po_q31_config_t *config,
                                       bm_algo_q31_t voltage_q31,
                                       bm_algo_q31_t current_q31) {
    bm_algo_q31_t power;

    if (state == NULL || config == NULL) {
        return voltage_q31;
    }

    power = mul_q31(voltage_q31, current_q31);
    if (power < state->prev_power_q31) {
        state->direction = -state->direction;
    }
    state->prev_power_q31 = power;

    state->v_ref_q31 = saturate_q31_i64(
        (int64_t)state->v_ref_q31 +
        (int64_t)state->direction * (int64_t)config->step_v_q31);
    state->v_ref_q31 = bm_algo_clamp_q31(state->v_ref_q31,
                                         config->v_min_q31,
                                         config->v_max_q31);
    return state->v_ref_q31;
}

void bm_algo_mppt_ic_q31_reset(bm_algo_mppt_ic_q31_state_t *state,
                               bm_algo_q31_t v_init_q31) {
    if (state != NULL) {
        state->v_ref_q31 = v_init_q31;
        state->prev_v_q31 = v_init_q31;
        state->prev_i_q31 = 0;
    }
}

bm_algo_q31_t bm_algo_mppt_ic_q31_step(bm_algo_mppt_ic_q31_state_t *state,
                                       const bm_algo_mppt_ic_q31_config_t *config,
                                       bm_algo_q31_t voltage_q31,
                                       bm_algo_q31_t current_q31) {
    bm_algo_q31_t dv;
    bm_algo_q31_t di;
    int64_t lhs;
    int64_t rhs;

    if (state == NULL || config == NULL) {
        return voltage_q31;
    }

    dv = voltage_q31 - state->prev_v_q31;
    di = current_q31 - state->prev_i_q31;

    if (dv != 0 && voltage_q31 != 0) {
        lhs = (int64_t)di * (int64_t)voltage_q31;
        rhs = -(int64_t)current_q31 * (int64_t)dv;
        if (lhs >= rhs) {
            state->v_ref_q31 = saturate_q31_i64(
                (int64_t)state->v_ref_q31 + (int64_t)config->step_v_q31);
        } else {
            state->v_ref_q31 = saturate_q31_i64(
                (int64_t)state->v_ref_q31 - (int64_t)config->step_v_q31);
        }
    }

    state->prev_v_q31 = voltage_q31;
    state->prev_i_q31 = current_q31;
    state->v_ref_q31 = bm_algo_clamp_q31(state->v_ref_q31,
                                         config->v_min_q31,
                                         config->v_max_q31);
    return state->v_ref_q31;
}

void bm_algo_pid2_q15_reset(bm_algo_pid2_q15_state_t *state, bm_algo_q15_t output) {
    if (state != NULL) {
        state->integrator = 0;
        state->prev_measurement = 0;
        state->d_filtered = 0;
        state->output = output;
    }
}

bm_algo_q15_t bm_algo_pid2_q15_step(bm_algo_pid2_q15_state_t *state,
                                    const bm_algo_pid2_q15_config_t *config,
                                    bm_algo_q15_t reference_q15,
                                    bm_algo_q15_t measurement_q15,
                                    bm_algo_q15_t dt_q15) {
    bm_algo_q15_t error_i;
    bm_algo_q15_t p_term;
    bm_algo_q15_t d_raw;
    bm_algo_q15_t d_term;
    bm_algo_q15_t u_unsat;
    bm_algo_q15_t u_sat;
    bm_algo_q15_t alpha;
    int32_t ref_weighted;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return 0;
    }

    error_i = reference_q15 - measurement_q15;
    ref_weighted = (int32_t)mul_q15(config->b_q15, reference_q15) -
                   (int32_t)measurement_q15;
    p_term = mul_q15(config->kp_q15, saturate_q15_i32(ref_weighted));

    state->integrator = saturate_q15_i32(
        (int32_t)state->integrator + (int32_t)mul_q15(error_i, dt_q15));
    state->integrator = bm_algo_clamp_q15(state->integrator,
                                          config->integrator_min,
                                          config->integrator_max);

    d_raw = div_q15((int32_t)measurement_q15 - (int32_t)state->prev_measurement,
                    dt_q15);
    d_raw = (bm_algo_q15_t)(-(int32_t)d_raw);
    state->prev_measurement = measurement_q15;
    alpha = bm_algo_clamp_q15(config->d_filter_coeff_q15, 0, BM_ALGO_Q15_ONE);
    state->d_filtered = saturate_q15_i32(
        (int32_t)state->d_filtered +
        (int32_t)mul_q15(alpha,
                         saturate_q15_i32((int32_t)d_raw -
                                          (int32_t)state->d_filtered)));
    d_term = mul_q15(config->kd_q15, state->d_filtered);

    u_unsat = saturate_q15_i32((int32_t)p_term +
                               (int32_t)mul_q15(config->ki_q15, state->integrator) +
                               (int32_t)d_term);
    u_sat = bm_algo_clamp_q15(u_unsat, config->out_min, config->out_max);

    if (config->ki_q15 != 0 && u_sat != u_unsat) {
        state->integrator = div_q15((int32_t)u_sat - (int32_t)p_term -
                                    (int32_t)d_term,
                                    config->ki_q15);
        state->integrator = bm_algo_clamp_q15(state->integrator,
                                              config->integrator_min,
                                              config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

void bm_algo_pid2_q31_reset(bm_algo_pid2_q31_state_t *state, bm_algo_q31_t output) {
    if (state != NULL) {
        state->integrator = 0;
        state->prev_measurement = 0;
        state->d_filtered = 0;
        state->output = output;
    }
}

bm_algo_q31_t bm_algo_pid2_q31_step(bm_algo_pid2_q31_state_t *state,
                                    const bm_algo_pid2_q31_config_t *config,
                                    bm_algo_q31_t reference_q31,
                                    bm_algo_q31_t measurement_q31,
                                    bm_algo_q31_t dt_q31) {
    bm_algo_q31_t error_i;
    bm_algo_q31_t p_term;
    bm_algo_q31_t d_raw;
    bm_algo_q31_t d_term;
    bm_algo_q31_t u_unsat;
    bm_algo_q31_t u_sat;
    bm_algo_q31_t alpha;
    bm_algo_q31_t ref_weighted;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return 0;
    }

    error_i = reference_q31 - measurement_q31;
    ref_weighted = saturate_q31_i64(
        (int64_t)mul_q31(config->b_q31, reference_q31) - (int64_t)measurement_q31);
    p_term = mul_q31(config->kp_q31, ref_weighted);

    state->integrator = saturate_q31_i64(
        (int64_t)state->integrator + (int64_t)mul_q31(error_i, dt_q31));
    state->integrator = bm_algo_clamp_q31(state->integrator,
                                          config->integrator_min,
                                          config->integrator_max);

    d_raw = div_q31((int64_t)measurement_q31 - (int64_t)state->prev_measurement,
                    dt_q31);
    d_raw = (bm_algo_q31_t)(-(int64_t)d_raw);
    state->prev_measurement = measurement_q31;
    alpha = bm_algo_clamp_q31(config->d_filter_alpha_q31, 0, BM_ALGO_Q31_ONE);
    state->d_filtered = saturate_q31_i64(
        (int64_t)state->d_filtered +
        (int64_t)mul_q31(
            alpha,
            saturate_q31_i64((int64_t)d_raw - (int64_t)state->d_filtered)));
    d_term = mul_q31(config->kd_q31, state->d_filtered);

    u_unsat = saturate_q31_i64((int64_t)p_term +
                               (int64_t)mul_q31(config->ki_q31, state->integrator) +
                               (int64_t)d_term);
    u_sat = bm_algo_clamp_q31(u_unsat, config->out_min, config->out_max);

    if (config->ki_q31 != 0 && u_sat != u_unsat) {
        state->integrator = div_q31(
            (int64_t)u_sat - (int64_t)p_term - (int64_t)d_term,
            config->ki_q31);
        state->integrator = bm_algo_clamp_q31(state->integrator,
                                              config->integrator_min,
                                              config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

void bm_algo_range_monitor_q31_reset(bm_algo_range_monitor_q31_state_t *state,
                                     bm_algo_q31_t v_q31) {
    if (state != NULL) {
        state->prev_q31 = v_q31;
        state->fault_flags = 0u;
    }
}

uint32_t bm_algo_range_monitor_q31_step(
    bm_algo_range_monitor_q31_state_t *state,
    const bm_algo_range_monitor_q31_config_t *config,
    bm_algo_q31_t sample_q31,
    bm_algo_q31_t dt_q31) {
    bm_algo_q31_t rate;
    uint32_t flags = 0u;

    if (state == NULL || config == NULL) {
        return 0u;
    }

    if (sample_q31 < config->min_v_q31) {
        flags |= BM_ALGO_FAULT_UNDER_RANGE;
    }
    if (sample_q31 > config->max_v_q31) {
        flags |= BM_ALGO_FAULT_OVER_RANGE;
    }

    if (dt_q31 > 0) {
        rate = div_q31((int64_t)sample_q31 - (int64_t)state->prev_q31,
                       dt_q31);
        if (abs_q31_val(rate) > config->max_rate_per_s_q31) {
            flags |= BM_ALGO_FAULT_RATE;
        }
    }

    if (sample_q31 == state->prev_q31) {
        flags |= BM_ALGO_FAULT_FROZEN;
    }

    state->prev_q31 = sample_q31;
    state->fault_flags = flags;
    return flags;
}

int bm_algo_smith_predictor_q15_init(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config,
    bm_algo_q15_t *delay_line_q15,
    uint32_t line_len) {
    if (state == NULL || config == NULL || delay_line_q15 == NULL ||
        config->delay_steps == 0u || line_len < config->delay_steps) {
        return -1;
    }
    state->u_delay_line_q15 = delay_line_q15;
    state->line_len = line_len;
    state->delay_steps = config->delay_steps;
    bm_algo_smith_predictor_q15_reset(state, config);
    return 0;
}

void bm_algo_smith_predictor_q15_reset(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config) {
    uint32_t i;

    if (state == NULL || state->u_delay_line_q15 == NULL || config == NULL) {
        return;
    }
    if (config->delay_steps == 0u ||
        config->delay_steps > state->line_len ||
        config->delay_steps != state->delay_steps) {
        return;
    }
    for (i = 0u; i < config->delay_steps; ++i) {
        state->u_delay_line_q15[i] = 0;
    }
    state->head = 0u;
}

bm_algo_q15_t bm_algo_smith_predictor_q15_step(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config,
    bm_algo_q15_t reference_q15,
    bm_algo_q15_t measurement_q15,
    bm_algo_q15_t u_controller_q15) {
    bm_algo_smith_predictor_config_t fcfg;
    bm_algo_smith_predictor_state_t fst;
    float delay_f[8];
    uint32_t i;
    float err;

    if (state == NULL || config == NULL || state->u_delay_line_q15 == NULL ||
        config->delay_steps == 0u ||
        config->delay_steps > state->line_len ||
        config->delay_steps > 8u) {
        return saturate_q15_i32((int32_t)reference_q15 - (int32_t)measurement_q15);
    }

    fcfg.model_gain = bm_algo_q15_to_float(config->model_gain_q15);
    fcfg.delay_steps = config->delay_steps;
    for (i = 0u; i < config->delay_steps; ++i) {
        delay_f[i] = bm_algo_q15_to_float(state->u_delay_line_q15[i]);
    }
    fst.u_delay_line = delay_f;
    fst.line_len = config->delay_steps;
    fst.delay_steps = config->delay_steps;
    fst.head = state->head;
    fst.y_model = 0.0f;
    fst.y_delayed = 0.0f;
    err = bm_algo_smith_predictor_step(
        &fst, &fcfg,
        bm_algo_q15_to_float(reference_q15),
        bm_algo_q15_to_float(measurement_q15),
        bm_algo_q15_to_float(u_controller_q15));
    for (i = 0u; i < config->delay_steps; ++i) {
        state->u_delay_line_q15[i] = bm_algo_float_to_q15(delay_f[i]);
    }
    state->head = fst.head;
    return bm_algo_float_to_q15(err);
}

int bm_algo_smith_predictor_q31_init(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config,
    bm_algo_q31_t *delay_line_q31,
    uint32_t line_len) {
    if (state == NULL || config == NULL || delay_line_q31 == NULL ||
        config->delay_steps == 0u || line_len < config->delay_steps) {
        return -1;
    }
    state->u_delay_line_q31 = delay_line_q31;
    state->line_len = line_len;
    state->delay_steps = config->delay_steps;
    bm_algo_smith_predictor_q31_reset(state, config);
    return 0;
}

void bm_algo_smith_predictor_q31_reset(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config) {
    uint32_t i;

    if (state == NULL || state->u_delay_line_q31 == NULL || config == NULL) {
        return;
    }
    if (config->delay_steps == 0u ||
        config->delay_steps > state->line_len ||
        config->delay_steps != state->delay_steps) {
        return;
    }
    for (i = 0u; i < config->delay_steps; ++i) {
        state->u_delay_line_q31[i] = 0;
    }
    state->head = 0u;
}

bm_algo_q31_t bm_algo_smith_predictor_q31_step(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config,
    bm_algo_q31_t reference_q31,
    bm_algo_q31_t measurement_q31,
    bm_algo_q31_t u_controller_q31) {
    bm_algo_smith_predictor_config_t fcfg;
    bm_algo_smith_predictor_state_t fst;
    float delay_f[8];
    uint32_t i;
    float err;

    if (state == NULL || config == NULL || state->u_delay_line_q31 == NULL ||
        config->delay_steps == 0u ||
        config->delay_steps > state->line_len ||
        config->delay_steps > 8u) {
        return saturate_q31_i64((int64_t)reference_q31 - (int64_t)measurement_q31);
    }

    fcfg.model_gain = bm_algo_q31_to_float(config->model_gain_q31);
    fcfg.delay_steps = config->delay_steps;
    for (i = 0u; i < config->delay_steps; ++i) {
        delay_f[i] = bm_algo_q31_to_float(state->u_delay_line_q31[i]);
    }
    fst.u_delay_line = delay_f;
    fst.line_len = config->delay_steps;
    fst.delay_steps = config->delay_steps;
    fst.head = state->head;
    fst.y_model = 0.0f;
    fst.y_delayed = 0.0f;
    err = bm_algo_smith_predictor_step(
        &fst, &fcfg,
        bm_algo_q31_to_float(reference_q31),
        bm_algo_q31_to_float(measurement_q31),
        bm_algo_q31_to_float(u_controller_q31));
    for (i = 0u; i < config->delay_steps; ++i) {
        state->u_delay_line_q31[i] = bm_algo_float_to_q31(delay_f[i]);
    }
    state->head = fst.head;
    return bm_algo_float_to_q31(err);
}

void bm_algo_sogi_pll_q15_reset(bm_algo_sogi_pll_q15_state_t *state,
                                const bm_algo_sogi_pll_q15_config_t *config) {
    bm_algo_sogi_pll_state_t fst;
    bm_algo_sogi_pll_config_t fcfg;

    if (state == NULL || config == NULL) {
        return;
    }

    fcfg.nominal_omega_rad_s = bm_algo_q15_to_float(config->nominal_omega_q15);
    fcfg.k_sogi = bm_algo_q15_to_float(config->k_sogi_q15);
    fcfg.k_pll = bm_algo_q15_to_float(config->k_pll_q15);
    bm_algo_sogi_pll_reset(&fst, &fcfg);
    state->theta_rad = fst.theta_rad;
    state->omega_rad_s = fst.omega_rad_s;
    state->v_alpha = fst.v_alpha;
    state->v_beta = fst.v_beta;
    state->integrator = fst.integrator;
    state->theta_rad_q15 = bm_algo_float_to_q15(fst.theta_rad);
    state->omega_rad_s_q15 = bm_algo_float_to_q15(fst.omega_rad_s);
}

void bm_algo_sogi_pll_q15_step(bm_algo_sogi_pll_q15_state_t *state,
                               const bm_algo_sogi_pll_q15_config_t *config,
                               bm_algo_q15_t v_input_q15,
                               bm_algo_q15_t dt_q15) {
    bm_algo_sogi_pll_state_t fst;
    bm_algo_sogi_pll_config_t fcfg;

    if (state == NULL || config == NULL || dt_q15 <= 0) {
        return;
    }

    fcfg.nominal_omega_rad_s = bm_algo_q15_to_float(config->nominal_omega_q15);
    fcfg.k_sogi = bm_algo_q15_to_float(config->k_sogi_q15);
    fcfg.k_pll = bm_algo_q15_to_float(config->k_pll_q15);
    fst.theta_rad = state->theta_rad;
    fst.omega_rad_s = state->omega_rad_s;
    fst.v_alpha = state->v_alpha;
    fst.v_beta = state->v_beta;
    fst.integrator = state->integrator;
    bm_algo_sogi_pll_step(
        &fst, &fcfg,
        bm_algo_q15_to_float(v_input_q15),
        bm_algo_q15_to_float(dt_q15));
    state->theta_rad = fst.theta_rad;
    state->omega_rad_s = fst.omega_rad_s;
    state->v_alpha = fst.v_alpha;
    state->v_beta = fst.v_beta;
    state->integrator = fst.integrator;
    state->theta_rad_q15 = bm_algo_float_to_q15(fst.theta_rad);
    state->omega_rad_s_q15 = bm_algo_float_to_q15(fst.omega_rad_s);
}

void bm_algo_sogi_pll_q31_reset(bm_algo_sogi_pll_q31_state_t *state,
                                const bm_algo_sogi_pll_q31_config_t *config) {
    bm_algo_sogi_pll_state_t fst;
    bm_algo_sogi_pll_config_t fcfg;

    if (state == NULL || config == NULL) {
        return;
    }

    fcfg.nominal_omega_rad_s = bm_algo_q31_to_float(config->nominal_omega_q31);
    fcfg.k_sogi = bm_algo_q31_to_float(config->k_sogi_q31);
    fcfg.k_pll = bm_algo_q31_to_float(config->k_pll_q31);
    bm_algo_sogi_pll_reset(&fst, &fcfg);
    state->theta_rad = fst.theta_rad;
    state->omega_rad_s = fst.omega_rad_s;
    state->v_alpha = fst.v_alpha;
    state->v_beta = fst.v_beta;
    state->integrator = fst.integrator;
    state->theta_rad_q31 = bm_algo_float_to_q31(fst.theta_rad);
    state->omega_rad_s_q31 = bm_algo_float_to_q31(fst.omega_rad_s);
}

void bm_algo_sogi_pll_q31_step(bm_algo_sogi_pll_q31_state_t *state,
                               const bm_algo_sogi_pll_q31_config_t *config,
                               bm_algo_q31_t v_input_q31,
                               bm_algo_q31_t dt_q31) {
    bm_algo_sogi_pll_state_t fst;
    bm_algo_sogi_pll_config_t fcfg;

    if (state == NULL || config == NULL || dt_q31 <= 0) {
        return;
    }

    fcfg.nominal_omega_rad_s = bm_algo_q31_to_float(config->nominal_omega_q31);
    fcfg.k_sogi = bm_algo_q31_to_float(config->k_sogi_q31);
    fcfg.k_pll = bm_algo_q31_to_float(config->k_pll_q31);
    fst.theta_rad = state->theta_rad;
    fst.omega_rad_s = state->omega_rad_s;
    fst.v_alpha = state->v_alpha;
    fst.v_beta = state->v_beta;
    fst.integrator = state->integrator;
    bm_algo_sogi_pll_step(
        &fst, &fcfg,
        bm_algo_q31_to_float(v_input_q31),
        bm_algo_q31_to_float(dt_q31));
    state->theta_rad = fst.theta_rad;
    state->omega_rad_s = fst.omega_rad_s;
    state->v_alpha = fst.v_alpha;
    state->v_beta = fst.v_beta;
    state->integrator = fst.integrator;
    state->theta_rad_q31 = bm_algo_float_to_q31(fst.theta_rad);
    state->omega_rad_s_q31 = bm_algo_float_to_q31(fst.omega_rad_s);
}

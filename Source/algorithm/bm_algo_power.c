/**
 * @file bm_algo_power.c
 * @brief 电源算法：SOGI-PLL、MPPT 与 RMS 实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            SOGI 前向欧拉稳定条件注释补充；
 *                                                bm_algo_sogi_pll_step 积分器增加对称限幅
 * 2026-06-23       1.2            zeh            SOGI 离散由前向欧拉改为双线性（Tustin）；
 *                                                bm_algo_sogi_pll_reset 新增导数缓存清零
 * 2026-06-23       1.3            zeh            修复 Tustin 历史导数项系数错误（h→T/2），
 *                                                消除 SOGI 递推发散（test_sogi_states_decay 回归）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_power.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>
#include <string.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

void bm_algo_sogi_pll_reset(bm_algo_sogi_pll_state_t *state,
                            const bm_algo_sogi_pll_config_t *config) {
    if (state == NULL) {
        return;
    }
    state->v_alpha      = 0.0f;
    state->v_beta       = 0.0f;
    state->theta_rad    = 0.0f;
    state->integrator   = 0.0f;
    state->d_alpha_prev = 0.0f; /* Tustin 导数缓存清零 */
    state->d_beta_prev  = 0.0f; /* Tustin 导数缓存清零 */
    if (config != NULL) {
        state->omega_rad_s = config->nominal_omega_rad_s;
    }
}

void bm_algo_sogi_pll_step(bm_algo_sogi_pll_state_t *state,
                           const bm_algo_sogi_pll_config_t *config,
                           float v_input,
                           float dt_s) {
    float vq;
    float k;
    float omega;
    float d_alpha;
    float d_beta;
    float h;
    float denom;
    float b1;
    float b2;
    float new_alpha;
    float new_beta;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return;
    }

    k = config->k_sogi;
    omega = state->omega_rad_s;

    /* ------------------------------------------------------------------
     * SOGI 双线性（Tustin）离散化
     *
     * 连续方程：
     *   dx1/dt = ω·(k·(v_in − x1) − x2)    [x1 = v_alpha]
     *   dx2/dt = ω·x1                        [x2 = v_beta ]
     *
     * Tustin 梯形积分：x[n] = x[n-1] + T/2·(f[n] + f[n-1])
     * 令 h = ω·dt_s/2，展开后联立求解 x1[n]、x2[n]：
     *
     *   (1 + h·k + h²)·x1[n] = (x1[n-1] + (T/2)·d_alpha_prev
     *                           + h·k·v_in[n])
     *                          − h·(x2[n-1] + (T/2)·d_beta_prev)
     *   x2[n] = x2[n-1] + (T/2)·d_beta_prev + h·x1[n]
     *
     * 其中 h = ω·T/2。注意：历史导数缓存 d_alpha_prev/d_beta_prev 存的是
     * 连续域真实导数 ẋ（已含 ω，见下方更新式），故其梯形积分项系数为 T/2，
     * 而非 h——若误用 h 等于把历史导数额外放大 ω 倍，会导致递推发散。
     * 行列式 denom = 1 + h·k + h²，对任意 h > 0 均正定，无条件稳定。
     * 离散极点模长满足 |z| < 1（双线性变换保留连续系统 Hurwitz 性质）。
     * ------------------------------------------------------------------ */
    h = omega * dt_s * 0.5f;
    denom = 1.0f + h * k + h * h;

    b1 = state->v_alpha + (dt_s * 0.5f) * state->d_alpha_prev + h * k * v_input;
    b2 = state->v_beta  + (dt_s * 0.5f) * state->d_beta_prev;

    new_alpha = (b1 - h * b2) / denom;
    new_beta  = b2 + h * new_alpha;

    /* 更新本拍导数缓存，供下一拍 Tustin 积分使用 */
    d_alpha = omega * (k * (v_input - new_alpha) - new_beta);
    d_beta  = omega * new_alpha;
    state->d_alpha_prev = d_alpha;
    state->d_beta_prev  = d_beta;

    state->v_alpha = new_alpha;
    state->v_beta  = new_beta;

    /* Park 到 dq，PLL 用 q 轴误差 */
    vq = -sinf(state->theta_rad) * state->v_alpha
         + cosf(state->theta_rad) * state->v_beta;
    state->integrator += config->k_pll * vq * dt_s;

    /* 积分器对称限幅：防止频率估计无限漂移。
     * 限幅比由 config->integrator_limit_ratio 配置（建议 0.2），
     * 0 时自动取 0.2，即允许偏差 ±20% 额定角频率。*/
    {
        float limit_ratio;
        float int_limit;

        limit_ratio = (config->integrator_limit_ratio > 0.0f)
                      ? config->integrator_limit_ratio : 0.2f;
        int_limit = config->nominal_omega_rad_s * limit_ratio;
        state->integrator = bm_algo_clamp_f(state->integrator,
                                            -int_limit, int_limit);
    }

    state->omega_rad_s = config->nominal_omega_rad_s + state->integrator;
    state->theta_rad += state->omega_rad_s * dt_s;

    state->theta_rad = bm_algo_angle_wrap_0_2pi_rad(state->theta_rad);
}

void bm_algo_mppt_po_reset(bm_algo_mppt_po_state_t *state, float v_init) {
    if (state != NULL) {
        state->v_ref = v_init;
        state->prev_power = 0.0f;
        state->direction = 1;
    }
}

float bm_algo_mppt_po_step(bm_algo_mppt_po_state_t *state,
                           const bm_algo_mppt_po_config_t *config,
                           float voltage,
                           float current) {
    float power;

    if (state == NULL || config == NULL) {
        return voltage;
    }

    power = voltage * current;
    if (power < state->prev_power) {
        state->direction = -state->direction;
    }
    state->prev_power = power;

    state->v_ref += (float)state->direction * config->step_v;
    state->v_ref = bm_algo_clamp_f(state->v_ref, config->v_min, config->v_max);
    return state->v_ref;
}

void bm_algo_mppt_ic_reset(bm_algo_mppt_ic_state_t *state, float v_init) {
    if (state != NULL) {
        state->v_ref = v_init;
        state->prev_v = v_init;
        state->prev_i = 0.0f;
    }
}

float bm_algo_mppt_ic_step(bm_algo_mppt_ic_state_t *state,
                           const bm_algo_mppt_ic_config_t *config,
                           float voltage,
                           float current) {
    float dv;
    float di;

    if (state == NULL || config == NULL) {
        return voltage;
    }

    dv = voltage - state->prev_v;
    di = current - state->prev_i;

    if (dv != 0.0f && fabsf(voltage) > 1e-12f) {
        if (di / dv >= -current / voltage) {
            state->v_ref += config->step_v;
        } else {
            state->v_ref -= config->step_v;
        }
    }

    state->prev_v = voltage;
    state->prev_i = current;
    state->v_ref = bm_algo_clamp_f(state->v_ref, config->v_min, config->v_max);
    return state->v_ref;
}

int bm_algo_rms_init(bm_algo_rms_state_t *state,
                     const bm_algo_rms_config_t *config,
                     float *buffer,
                     uint32_t buflen) {
    if (state == NULL || config == NULL || buffer == NULL ||
        buflen < config->window_samples || config->window_samples == 0u) {
        return -1;
    }
    state->buffer = buffer;
    state->buflen = buflen;
    state->window_samples = config->window_samples;
    bm_algo_rms_reset(state);
    return 0;
}

void bm_algo_rms_reset(bm_algo_rms_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->sum_sq = 0.0f;
    state->count = 0u;
    state->index = 0u;
    if (state->buffer != NULL && state->buflen > 0u) {
        memset(state->buffer, 0, state->buflen * sizeof(float));
    }
}

float bm_algo_rms_step(bm_algo_rms_state_t *state,
                       const bm_algo_rms_config_t *config,
                       float sample) {
    float old;
    uint32_t win;

    if (state == NULL || config == NULL || state->buffer == NULL ||
        config->window_samples == 0u ||
        config->window_samples > state->buflen ||
        config->window_samples != state->window_samples ||
        state->index >= state->window_samples) {
        return sample;
    }

    win = config->window_samples;
    old = state->buffer[state->index];
    state->buffer[state->index] = sample;
    state->sum_sq += sample * sample - old * old;

    state->index = (state->index + 1u) % win;
    if (state->count < win) {
        state->count++;
    }

    if (state->sum_sq < 0.0f && state->sum_sq > -1e-6f) {
        state->sum_sq = 0.0f;
    }
    if (state->sum_sq < 0.0f || state->count == 0u) {
        return 0.0f;
    }
    return sqrtf(state->sum_sq / (float)state->count);
}

void bm_algo_power_instant(float v, float i, float *p, float *q) {
    if (p != NULL) {
        *p = v * i;
    }
    if (q != NULL) {
        *q = 0.0f;
    }
}

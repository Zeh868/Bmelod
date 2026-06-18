/**
 * @file bm_algo_power.c
 * @brief 电源算法：SOGI-PLL、MPPT 与 RMS 实现
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
    state->v_alpha = 0.0f;
    state->v_beta = 0.0f;
    state->theta_rad = 0.0f;
    state->integrator = 0.0f;
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

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return;
    }

    k = config->k_sogi;
    omega = state->omega_rad_s;

    /* Continuous SOGI state equations, integrated with forward Euler. */
    d_alpha = omega * (k * (v_input - state->v_alpha) - state->v_beta);
    d_beta = omega * state->v_alpha;
    state->v_alpha += d_alpha * dt_s;
    state->v_beta += d_beta * dt_s;

    /* Park 到 dq，PLL 用 q 轴误差 */
    vq = -sinf(state->theta_rad) * state->v_alpha
         + cosf(state->theta_rad) * state->v_beta;
    state->integrator += config->k_pll * vq * dt_s;
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

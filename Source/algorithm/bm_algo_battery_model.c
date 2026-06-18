/**
 * @file bm_algo_battery_model.c
 * @brief 电池等效模型实现
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
#include "bm/algorithm/bm_algo_battery_model.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

void bm_algo_soc_ekf_reset(bm_algo_soc_ekf_state_t *state, float soc_init) {
    if (state == NULL) {
        return;
    }
    state->soc = bm_algo_clamp_f(soc_init, 0.0f, 1.0f);
    state->bias_a = 0.0f;
    state->p00 = 0.01f;
    state->p01 = 0.0f;
    state->p10 = 0.0f;
    state->p11 = 0.01f;
}

void bm_algo_soc_ekf_predict(bm_algo_soc_ekf_state_t *state,
                             const bm_algo_soc_ekf_config_t *config,
                             float current_a,
                             float dt_s) {
    float dsoc;
    float bias_gain;
    float p00;
    float p01;
    float p10;
    float p11;

    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        config->nominal_capacity_ah <= 0.0f) {
        return;
    }

    bias_gain = config->coulomb_efficiency * dt_s /
                (config->nominal_capacity_ah * 3600.0f);
    dsoc = (current_a - state->bias_a) * bias_gain;
    state->soc = bm_algo_clamp_f(state->soc + dsoc, 0.0f, 1.0f);

    p00 = state->p00;
    p01 = state->p01;
    p10 = state->p10;
    p11 = state->p11;

    state->p00 = p00 - bias_gain * (p01 + p10)
                 + bias_gain * bias_gain * p11
                 + config->q_soc * dt_s;
    state->p01 = p01 - bias_gain * p11;
    state->p10 = p10 - bias_gain * p11;
    state->p11 = p11 + config->q_bias * dt_s;
}

void bm_algo_soc_ekf_update_voltage(bm_algo_soc_ekf_state_t *state,
                                    const bm_algo_soc_ekf_config_t *config,
                                    float terminal_v,
                                    float ocv_from_soc) {
    float y;
    float s;
    float k0;
    float k1;
    float p00;
    float p01;
    float p10;
    float p11;
    float slope;

    if (state == NULL || config == NULL) {
        return;
    }

    slope = config->ocv_slope_v_per_soc;
    if (slope <= 0.0f) {
        slope = 0.5f;
    }
    y = terminal_v - ocv_from_soc;
    s = state->p00 * slope * slope + config->r_v;
    if (s <= 1e-9f) {
        return;
    }

    k0 = state->p00 * slope / s;
    k1 = state->p10 * slope / s;

    p00 = state->p00;
    p01 = state->p01;
    p10 = state->p10;
    p11 = state->p11;

    state->soc += k0 * y;
    state->bias_a += k1 * y;
    state->soc = bm_algo_clamp_f(state->soc, 0.0f, 1.0f);

    state->p00 = p00 - k0 * slope * p00;
    state->p01 = p01 - k0 * slope * p01;
    state->p10 = p10 - k1 * slope * p00;
    state->p11 = p11 - k1 * slope * p01;
    state->p01 = 0.5f * (state->p01 + state->p10);
    state->p10 = state->p01;
}

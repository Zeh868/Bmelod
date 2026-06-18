/**
 * @file bm_algo_battery.c
 * @brief 电池算法实现
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
#include "bm/algorithm/bm_algo_battery.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

void bm_algo_coulomb_reset(bm_algo_coulomb_state_t *state, float soc_init) {
    if (state != NULL) {
        state->soc = soc_init;
        state->charge_ah = 0.0f;
    }
}

float bm_algo_coulomb_step(bm_algo_coulomb_state_t *state,
                           const bm_algo_coulomb_config_t *config,
                           float current_a,
                           float dt_s) {
    float delta_soc;
    float cap;

    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        config->nominal_capacity_ah <= 0.0f) {
        return state != NULL ? state->soc : 0.0f;
    }

    cap = config->nominal_capacity_ah;
    state->charge_ah += current_a * dt_s / 3600.0f;
    delta_soc = (current_a * dt_s / 3600.0f) * config->coulomb_efficiency / cap;

    state->soc += delta_soc;
    state->soc = bm_algo_clamp_f(state->soc, config->soc_min, config->soc_max);
    return state->soc;
}

static float lut_interp(const float *x, const float *y, uint32_t n, float xv) {
    uint32_t i;

    if (n == 0u || x == NULL || y == NULL) {
        return 0.0f;
    }
    if (xv <= x[0]) {
        return y[0];
    }
    if (xv >= x[n - 1u]) {
        return y[n - 1u];
    }

    for (i = 0u; i + 1u < n; ++i) {
        if (xv >= x[i] && xv <= x[i + 1u]) {
            float span = x[i + 1u] - x[i];
            float t;

            if (span <= 0.0f) {
                return y[i];
            }
            t = (xv - x[i]) / span;
            return y[i] + t * (y[i + 1u] - y[i]);
        }
    }
    return y[n - 1u];
}

float bm_algo_ocv_lookup_soc(const bm_algo_ocv_table_t *table, float ocv_v) {
    if (table == NULL) {
        return 0.0f;
    }
    return lut_interp(table->ocv_table, table->soc_table,
                      table->point_count, ocv_v);
}

float bm_algo_ocv_lookup_voltage(const bm_algo_ocv_table_t *table, float soc) {
    if (table == NULL) {
        return 0.0f;
    }
    return lut_interp(table->soc_table, table->ocv_table,
                      table->point_count, soc);
}

float bm_algo_soc_fusion_step(float soc_coulomb,
                              float soc_ocv,
                              const bm_algo_soc_fusion_config_t *config) {
    float w;

    if (config == NULL) {
        return soc_coulomb;
    }

    w = bm_algo_clamp_f(config->ocv_weight, 0.0f, 1.0f);
    return (1.0f - w) * soc_coulomb + w * soc_ocv;
}

void bm_algo_soh_reset(bm_algo_soh_state_t *state,
                       const bm_algo_soh_config_t *config) {
    if (state == NULL) {
        return;
    }
    state->learned_capacity_ah = config != NULL ? config->initial_capacity_ah : 0.0f;
    state->cycle_count = 0.0f;
}

float bm_algo_soh_update(bm_algo_soh_state_t *state,
                         const bm_algo_soh_config_t *config,
                         float discharged_ah) {
    float soh;

    if (state == NULL || config == NULL || config->initial_capacity_ah <= 0.0f) {
        return 1.0f;
    }

    if (discharged_ah > 0.0f) {
        state->learned_capacity_ah = discharged_ah;
        state->cycle_count += 1.0f;
    }

    soh = state->learned_capacity_ah / config->initial_capacity_ah;
    return bm_algo_clamp_f(soh, 0.0f, 1.0f);
}

float bm_algo_battery_temp_capacity_ah(float nominal_capacity_ah,
                                       float temp_c,
                                       const bm_algo_battery_temp_config_t *config) {
    float factor;

    if (config == NULL || nominal_capacity_ah <= 0.0f) {
        return nominal_capacity_ah;
    }

    factor = 1.0f + config->capacity_coeff_per_c * (temp_c - config->ref_temp_c);
    if (factor < 0.1f) {
        factor = 0.1f;
    }
    return nominal_capacity_ah * factor;
}

float bm_algo_battery_temp_compensate_ocv(float ocv_v,
                                          float temp_c,
                                          const bm_algo_battery_temp_config_t *config) {
    if (config == NULL) {
        return ocv_v;
    }
    return ocv_v - config->ocv_shift_v_per_c * (temp_c - config->ref_temp_c);
}

/**
 * @file bmp_algo_bms.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief BMS SOC 融合实现
 */
#include "bmp/algo/bmp_algo_bms.h"

#include "bm/algorithm/bm_algo_battery.h"

#include <string.h>

static const float s_soc_table[] = { 0.0f, 0.5f, 1.0f };
static const float s_ocv_table[] = { 3.0f, 3.6f, 4.2f };

static float bmp_bms_voltage_to_soc(float voltage_v) {
    bm_algo_ocv_table_t table;

    table.soc_table = s_soc_table;
    table.ocv_table = s_ocv_table;
    table.point_count = 3u;
    return bm_algo_ocv_lookup_soc(&table, voltage_v);
}

int bmp_bms_fusion_init(bmp_bms_state_t *state,
                        const bmp_bms_config_t *config,
                        float soc_init) {
    if (state == NULL || config == NULL || config->nominal_capacity_ah <= 0.0f) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    if (soc_init < 0.0f) {
        soc_init = 0.0f;
    }
    if (soc_init > 1.0f) {
        soc_init = 1.0f;
    }
    state->soc_coulomb = soc_init;
    state->initialized = 1u;
    return 0;
}

int bmp_bms_fusion_step(bmp_bms_state_t *state,
                        const bmp_bms_config_t *config,
                        float current_a,
                        float voltage_v,
                        float dt_s,
                        float *soc_out) {
    bm_algo_coulomb_config_t coulomb_cfg;
    bm_algo_coulomb_state_t coulomb_st;
    bm_algo_soc_fusion_config_t fusion_cfg;
    float soc_ocv;
    float fused;

    if (state == NULL || config == NULL || soc_out == NULL ||
        state->initialized == 0u || dt_s <= 0.0f) {
        return -1;
    }
    coulomb_cfg.nominal_capacity_ah = config->nominal_capacity_ah;
    coulomb_cfg.coulomb_efficiency = 1.0f;
    coulomb_cfg.soc_min = 0.0f;
    coulomb_cfg.soc_max = 1.0f;
    coulomb_st.soc = state->soc_coulomb;
    coulomb_st.charge_ah = state->charge_ah;
    state->soc_coulomb = bm_algo_coulomb_step(&coulomb_st, &coulomb_cfg,
                                              current_a, dt_s);
    state->charge_ah = coulomb_st.charge_ah;
    soc_ocv = bmp_bms_voltage_to_soc(voltage_v);
    fusion_cfg.ocv_weight = config->ocv_weight;
    fused = bm_algo_soc_fusion_step(state->soc_coulomb, soc_ocv, &fusion_cfg);
    *soc_out = fused;
    return 0;
}

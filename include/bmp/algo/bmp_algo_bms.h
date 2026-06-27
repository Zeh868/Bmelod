/**
 * @file bmp_algo_bms.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief K2 · 闭源 · 需 bm_mp 的 BMS SOC 融合：库仑计量 + OCV 加权
 */
#ifndef BMP_ALGO_BMS_H
#define BMP_ALGO_BMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float nominal_capacity_ah;
    float ocv_weight;
} bmp_bms_config_t;

typedef struct {
    float soc_coulomb;
    float charge_ah;
    uint8_t initialized;
    uint8_t reserved[3];
} bmp_bms_state_t;

int bmp_bms_fusion_init(bmp_bms_state_t *state,
                        const bmp_bms_config_t *config,
                        float soc_init);

int bmp_bms_fusion_step(bmp_bms_state_t *state,
                        const bmp_bms_config_t *config,
                        float current_a,
                        float voltage_v,
                        float dt_s,
                        float *soc_out);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_BMS_H */

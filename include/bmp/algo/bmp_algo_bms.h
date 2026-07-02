/**
 * @file bmp_algo_bms.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief K2 · 闭源 · 需 bm_mp 的 BMS SOC 融合：库仑计量 + OCV 加权
 */
#ifndef BMP_ALGO_BMS_H
#define BMP_ALGO_BMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BMS 融合配置
 *
 * OCV 表三个字段全部提供（非 NULL 指针且 point_count >= 2）时启用自定义
 * 电池化学曲线；否则回退内部默认 3 点表 {3.0,3.6,4.2}V ↔ {0,0.5,1.0}，
 * 保证只填容量/权重的旧调用行为不变。soc_table 与 ocv_table 须等长、
 * 生命周期覆盖 fusion_step 调用期。
 */
typedef struct {
    float nominal_capacity_ah;      /**< 标称容量（Ah，必须 > 0） */
    float ocv_weight;               /**< OCV 融合权重 */
    const float *ocv_soc_table;     /**< SOC 断点数组，NULL=用默认表 */
    const float *ocv_voltage_table; /**< 对应 OCV 电压断点数组，NULL=用默认表 */
    uint32_t     ocv_point_count;   /**< 断点数（>=2 生效），0=用默认表 */
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

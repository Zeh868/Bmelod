/**
 * @file bm_algo_battery_model.h
 * @brief 电池等效模型：SOC + 电流偏置 EKF
 *
 * 与 bm_algo_battery 查表/库仑核配合，用于电压反馈修正。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_BATTERY_MODEL_H
#define BM_ALGO_BATTERY_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- SOC + 偏置 EKF ---------- */

typedef struct {
    float q_soc;
    float q_bias;
    float r_v;
    float coulomb_efficiency;
    float nominal_capacity_ah;
    float ocv_slope_v_per_soc; /**< d(OCV)/d(SOC)，用于电压观测雅可比 */
} bm_algo_soc_ekf_config_t;

typedef struct {
    float soc;
    float bias_a;
    float p00;
    float p01;
    float p10;
    float p11;
} bm_algo_soc_ekf_state_t;

void bm_algo_soc_ekf_reset(bm_algo_soc_ekf_state_t *state, float soc_init);
void bm_algo_soc_ekf_predict(bm_algo_soc_ekf_state_t *state,
                             const bm_algo_soc_ekf_config_t *config,
                             float current_a,
                             float dt_s);
void bm_algo_soc_ekf_update_voltage(bm_algo_soc_ekf_state_t *state,
                                    const bm_algo_soc_ekf_config_t *config,
                                    float terminal_v,
                                    float ocv_from_soc);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_BATTERY_MODEL_H */

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

/**
 * @brief SOC-EKF 配置参数
 *
 * 状态向量为 [soc, bias_a]（SOC 与电流传感器偏置）。
 */
typedef struct {
    float q_soc;                 /**< SOC 过程噪声协方差（调参项，典型值 1e-5） */
    float q_bias;                /**< 电流偏置过程噪声协方差（调参项，典型值 1e-6） */
    float r_v;                   /**< 电压观测噪声协方差（调参项，单位 V²） */
    float coulomb_efficiency;    /**< 库仑效率（充电 < 1.0，放电通常 = 1.0） */
    float nominal_capacity_ah;   /**< 电池额定容量（Ah） */
    float ocv_slope_v_per_soc;   /**< d(OCV)/d(SOC)，电压观测雅可比（V/1，典型值 0.3~1.0） */
} bm_algo_soc_ekf_config_t;

/**
 * @brief SOC-EKF 运行状态（2×2 协方差矩阵 + 状态估计）
 */
typedef struct {
    float soc;    /**< 当前 SOC 估计值（0.0 ~ 1.0） */
    float bias_a; /**< 当前电流偏置估计值（A） */
    float p00;    /**< 协方差矩阵 P[0][0]（SOC 方差） */
    float p01;    /**< 协方差矩阵 P[0][1]（交叉协方差） */
    float p10;    /**< 协方差矩阵 P[1][0]（交叉协方差） */
    float p11;    /**< 协方差矩阵 P[1][1]（偏置方差） */
} bm_algo_soc_ekf_state_t;

/**
 * @brief 复位 SOC-EKF，设置初始 SOC 并清零偏置与协方差
 *
 * 初始协方差固定为 P = diag(0.01, 0.01)。
 *
 * @param state    EKF 状态指针，为 NULL 时静默返回
 * @param soc_init 初始 SOC（自动钳位到 [0.0, 1.0]）
 */
void bm_algo_soc_ekf_reset(bm_algo_soc_ekf_state_t *state, float soc_init);

/**
 * @brief SOC-EKF 预测步：库仑积分 + 协方差传播
 *
 * 状态方程：soc(k+1) = soc(k) + η * (I - bias) * dt / (C_nom * 3600)
 * 协方差按线性化传播并叠加过程噪声 Q。
 *
 * @param state     EKF 状态指针，为 NULL 时静默返回
 * @param config    EKF 配置指针，为 NULL 时静默返回
 * @param current_a 当前电流（A，充电为正）
 * @param dt_s      时间步长（秒），须 > 0
 */
void bm_algo_soc_ekf_predict(bm_algo_soc_ekf_state_t *state,
                             const bm_algo_soc_ekf_config_t *config,
                             float current_a,
                             float dt_s);

/**
 * @brief SOC-EKF 电压更新步：卡尔曼增益修正 SOC 与偏置
 *
 * 观测方程：V_terminal ≈ OCV(soc) + 残差；雅可比为 ocv_slope_v_per_soc。
 * 更新后对 P01/P10 做对称化处理（P01 = P10 = (P01+P10)/2）。
 *
 * @param state         EKF 状态指针，为 NULL 时静默返回
 * @param config        EKF 配置指针，为 NULL 时静默返回
 * @param terminal_v    实测端电压（V）
 * @param ocv_from_soc  由当前 SOC 估计查表得到的开路电压（V）
 */
void bm_algo_soc_ekf_update_voltage(bm_algo_soc_ekf_state_t *state,
                                    const bm_algo_soc_ekf_config_t *config,
                                    float terminal_v,
                                    float ocv_from_soc);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_BATTERY_MODEL_H */

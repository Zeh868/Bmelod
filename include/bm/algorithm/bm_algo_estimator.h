/**
 * @file bm_algo_estimator.h
 * @brief 固定维度状态估算：一维卡尔曼与简易 EKF 接口
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            增加 1D UKF 与 EKF 创新门控
 * 2026-06-23       1.2            zeh            KF 更新分母阈值放宽为 1e-9f；UKF β 修正项补注释
 * 2026-06-23       1.3            zeh            落地 UKF β 协方差修正项：pzz 中 i=0 项使用 w0_cov
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_ESTIMATOR_H
#define BM_ALGO_ESTIMATOR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 一维卡尔曼滤波 ---------- */
typedef struct {
    float q;  /**< 过程噪声 */
    float r;  /**< 测量噪声 */
} bm_algo_kalman1d_config_t;

typedef struct {
    float x;
    float p;
} bm_algo_kalman1d_state_t;

void bm_algo_kalman1d_reset(bm_algo_kalman1d_state_t *state,
                            float x0,
                            float p0);
float bm_algo_kalman1d_predict(bm_algo_kalman1d_state_t *state,
                               const bm_algo_kalman1d_config_t *config);
float bm_algo_kalman1d_update(bm_algo_kalman1d_state_t *state,
                              const bm_algo_kalman1d_config_t *config,
                              float measurement);

/* ---------- 二维 EKF（位置-速度常速度模型） ---------- */
typedef struct {
    float q_pos;
    float q_vel;
    float r_pos;
} bm_algo_ekf_cv_config_t;

typedef struct {
    float pos;
    float vel;
    float p00, p01, p10, p11;
} bm_algo_ekf_cv_state_t;

void bm_algo_ekf_cv_reset(bm_algo_ekf_cv_state_t *state, float pos, float vel);
void bm_algo_ekf_cv_predict(bm_algo_ekf_cv_state_t *state,
                            const bm_algo_ekf_cv_config_t *config,
                            float dt_s);
void bm_algo_ekf_cv_update(bm_algo_ekf_cv_state_t *state,
                           const bm_algo_ekf_cv_config_t *config,
                           float pos_meas);

/* ---------- 一维 UKF（固定测量模型枚举，init 阶段选定） ---------- */
typedef enum {
    BM_ALGO_UKF1D_MEAS_IDENTITY = 0, /**< h(x) = x，等价线性 KF */
    BM_ALGO_UKF1D_MEAS_SQUARE   = 1  /**< h(x) = x^2 */
} bm_algo_ukf1d_measurement_model_t;

typedef struct {
    float q;
    float r;
    bm_algo_ukf1d_measurement_model_t measurement_model;
} bm_algo_ukf1d_config_t;

typedef struct {
    float x;
    float p;
} bm_algo_ukf1d_state_t;

void bm_algo_ukf1d_reset(bm_algo_ukf1d_state_t *state, float x0, float p0);
void bm_algo_ukf1d_predict(bm_algo_ukf1d_state_t *state,
                           const bm_algo_ukf1d_config_t *config);

/**
 * @brief 1D UKF 测量更新（含 β 协方差修正项）
 *
 * 权重定义（n=1，λ = α²·(1+κ)）：
 *   - 均值权  w0_mean = (λ-1)/λ，w1 = 0.5/λ（i=1,2 共用）
 *   - 协方差权 w0_cov  = w0_mean + (1 - α² + β)，仅用于 pzz/pxx 中 i=0 项
 *
 * 退化条件：β=0、α=1 时 w0_cov = w0_mean，行为与原实现完全一致。
 * 默认参数 α=1、β=2（高斯分布最优值），此时 w0_cov = w0_mean + 2。
 *
 * @param state       滤波器状态（x, p）
 * @param config      滤波器配置（r, q, measurement_model）
 * @param measurement 当前测量值
 * @return BM_ALGO_EKF_UPDATE_OK / BM_ALGO_EKF_UPDATE_INVALID
 */
int bm_algo_ukf1d_update(bm_algo_ukf1d_state_t *state,
                         const bm_algo_ukf1d_config_t *config,
                         float measurement);

/* ---------- EKF 创新门控 ---------- */
typedef struct {
    float innovation_threshold; /**< 归一化创新平方阈值（马氏距离平方） */
} bm_algo_ekf_gate_config_t;

#define BM_ALGO_EKF_UPDATE_OK      0
#define BM_ALGO_EKF_UPDATE_GATED  (-1)
#define BM_ALGO_EKF_UPDATE_INVALID (-2)

/**
 * @brief 判断创新是否通过门控
 *
 * @param innovation 创新（测量残差）
 * @param innovation_var 创新方差 S（须 > 0）
 * @param threshold 归一化创新平方上限
 * @return 1 接受；0 拒绝
 */
int bm_algo_ekf_gate_accept(float innovation,
                            float innovation_var,
                            float threshold);

/**
 * @brief 带创新门控的 CV-EKF 位置更新
 *
 * 门控失败时跳过协方差与状态更新，返回 BM_ALGO_EKF_UPDATE_GATED。
 *
 * @param gate 门控配置；NULL 时等同无门控更新
 * @return BM_ALGO_EKF_UPDATE_OK / GATED / INVALID
 */
int bm_algo_ekf_cv_update_gated(bm_algo_ekf_cv_state_t *state,
                                const bm_algo_ekf_cv_config_t *config,
                                float pos_meas,
                                const bm_algo_ekf_gate_config_t *gate);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_ESTIMATOR_H */

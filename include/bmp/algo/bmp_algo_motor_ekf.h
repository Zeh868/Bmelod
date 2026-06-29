/**
 * @file bmp_algo_motor_ekf.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief K2 · 闭源 · 需 bm_mp 的高级无感观测器：EKF 扩展卡尔曼滤波
 *
 * 基于 αβ 坐标系电机模型的状态估计，输出转子电角度与角速度。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 * Date       Version Author Description
 * 2026-06-17 1.0     zeh    首版 MP 工业 EKF 无感观测 API
 * 2026-06-17 1.1     zeh    完整 P 预测/更新，接入 q/r 协方差
 *
 */
#ifndef BMP_ALGO_MOTOR_EKF_H
#define BMP_ALGO_MOTOR_EKF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EKF 无感观测器运行配置 */
typedef struct {
    float rs_ohm;        /**< 定子电阻（Ω） */
    float ls_h;          /**< 定子电感（H），须 > 0 */
    float ke_v_rad_s;    /**< 反电动势常数（V·s/rad），用于转速估算 */
    float q_covariance;  /**< 过程噪声协方差（对角，>0；0 则用内部缺省） */
    float r_covariance;  /**< 测量噪声协方差（对角，>0；0 则用内部缺省） */
} bmp_motor_ekf_config_t;

/** EKF 无感观测器状态（调用方分配，禁止堆分配） */
typedef struct {
    float    i_alpha;     /**< α 轴电流估计（A） */
    float    i_beta;      /**< β 轴电流估计（A） */
    float    emf_alpha;   /**< α 轴反电动势估计（V） */
    float    emf_beta;    /**< β 轴反电动势估计（V） */
    float    theta_rad;   /**< 转子电角度估计（rad） */
    float    omega_rad_s; /**< 转子电角速度估计（rad/s） */
    float    p[4][4];     /**< 误差协方差矩阵 */
    uint8_t  initialized;
    uint8_t  reserved[3];
} bmp_motor_ekf_state_t;

/**
 * @brief 初始化 EKF 无感观测器状态
 *
 * @param state 状态对象，不可为 NULL
 * @param config 配置，不可为 NULL；ls_h 须 > 0
 * @return 0 成功；-1 参数无效
 */
int bmp_motor_ekf_init(bmp_motor_ekf_state_t *state,
                       const bmp_motor_ekf_config_t *config);

/**
 * @brief 执行一步 EKF 预测-更新并估算角度与角速度
 *
 * @param state 已初始化状态，不可为 NULL
 * @param config 配置，不可为 NULL
 * @param v_alpha α 轴施加电压（V）
 * @param v_beta β 轴施加电压（V）
 * @param i_alpha_meas α 轴电流测量（A）
 * @param i_beta_meas β 轴电流测量（A）
 * @param dt_s 步进周期（s），须 > 0
 * @return 0 成功；-1 参数无效或未初始化
 */
int bmp_motor_ekf_step(bmp_motor_ekf_state_t *state,
                       const bmp_motor_ekf_config_t *config,
                       float v_alpha,
                       float v_beta,
                       float i_alpha_meas,
                       float i_beta_meas,
                       float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_MOTOR_EKF_H */

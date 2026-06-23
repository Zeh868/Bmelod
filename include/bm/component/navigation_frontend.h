/**
 * @file navigation_frontend.h
 * @brief GNSS/IMU/轮速时间对齐与门控速度融合骨架
 *
 * 三源采样经时间戳对齐后，由 EKF 门控融合输出线速度估计。
 * gnss_weight/wheel_weight 须非负且和不超过合理范围；
 * EKF 噪声参数须正值。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            validate_config 噪声/权重校验；公共 API Doxygen；SPDX
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_NAVIGATION_FRONTEND_H
#define BM_NAVIGATION_FRONTEND_H

#include "bm/algorithm/bm_algo_estimator.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_NAV_FRONTEND_TEL_VALID      (1u << 0u)
#define BM_NAV_FRONTEND_TEL_GNSS_STALE (1u << 1u)
#define BM_NAV_FRONTEND_TEL_WHEEL_ONLY (1u << 2u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    velocity_m_s;
    float    gnss_velocity_m_s;
    float    wheel_velocity_m_s;
} bm_navigation_frontend_telemetry_t;

typedef int (*bm_navigation_read_gnss_fn)(void *user,
                                            float *velocity_m_s,
                                            uint32_t *timestamp_us);

typedef int (*bm_navigation_read_imu_fn)(void *user,
                                         float *gyro_rad_s,
                                         uint32_t *timestamp_us);

typedef int (*bm_navigation_read_wheel_fn)(void *user,
                                           float *rpm,
                                           uint32_t *timestamp_us);

typedef void (*bm_navigation_publish_fn)(
    void *user,
    const bm_navigation_frontend_telemetry_t *telemetry);

typedef struct {
    bm_navigation_read_gnss_fn read_gnss;
    void                      *read_gnss_user;
    bm_navigation_read_imu_fn  read_imu;
    void                      *read_imu_user;
    bm_navigation_read_wheel_fn read_wheel;
    void                      *read_wheel_user;
    bm_navigation_publish_fn   publish_telemetry;
    void                      *publish_telemetry_user;
} bm_navigation_frontend_resources_t;

typedef struct {
    float wheel_radius_m;
    uint32_t align_tolerance_us;
    float gnss_weight;
    float wheel_weight;
    float dt_s;
    bm_algo_ekf_gate_config_t gate;
    bm_algo_ekf_cv_config_t   ekf;
} bm_navigation_frontend_config_t;

typedef struct {
    float gnss_vel;
    float wheel_vel;
    float gyro_rad_s;
    uint32_t gnss_ts_us;
    uint32_t wheel_ts_us;
    uint32_t imu_ts_us;
    int have_gnss;
    int have_wheel;
    bm_algo_ekf_cv_state_t ekf;
    float fused_velocity_m_s;
    uint32_t step_count;
    bm_navigation_frontend_telemetry_t telemetry;
} bm_navigation_frontend_state_t;

typedef struct {
    bm_navigation_frontend_config_t    config;
    bm_navigation_frontend_resources_t resources;
    bm_navigation_frontend_state_t     state;
} bm_navigation_frontend_axis_t;

/**
 * @brief 校验导航前端配置合法性
 *
 * 检查 dt_s > 0、wheel_radius_m > 0、gnss_weight >= 0、
 * wheel_weight >= 0，以及 EKF 噪声参数（q_pos/q_vel/r_pos）均 > 0。
 *
 * @param config 待校验配置指针，不得为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数非法
 */
int  bm_navigation_frontend_validate_config(
    const bm_navigation_frontend_config_t *config);

/**
 * @brief 初始化导航前端轴
 *
 * 校验配置并执行 reset（含 EKF 状态清零）。
 *
 * @param axis 轴实例指针，不得为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int  bm_navigation_frontend_init(bm_navigation_frontend_axis_t *axis);

/**
 * @brief 复位导航前端状态
 *
 * 清零 EKF 状态、速度估计、时间戳及遥测缓冲。
 *
 * @param axis 轴实例指针；NULL 时直接返回
 */
void bm_navigation_frontend_reset(bm_navigation_frontend_axis_t *axis);

/**
 * @brief 执行一次导航前端融合步骤
 *
 * 依次采样三源（GNSS/IMU/轮速），做 EKF 预测，时间戳对齐后执行
 * 门控 EKF 更新或降级融合，结果写入 axis->state.fused_velocity_m_s
 * 并通过 publish_telemetry 发布遥测。
 *
 * @param axis 轴实例指针；NULL 时直接返回
 */
void bm_navigation_frontend_step(bm_navigation_frontend_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_NAVIGATION_FRONTEND_H */

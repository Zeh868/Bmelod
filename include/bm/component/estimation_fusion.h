/**
 * @file estimation_fusion.h
 * @brief 姿态/状态估算融合选择器（互补 / Mahony / EKF）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 */
#ifndef BM_ESTIMATION_FUSION_H
#define BM_ESTIMATION_FUSION_H

#include "bm/algorithm/bm_algo_estimator.h"
#include "bm/algorithm/bm_algo_fusion.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_EST_FUSION_TEL_VALID   (1u << 0u)
#define BM_EST_FUSION_TEL_NO_IMU  (1u << 1u)
#define BM_EST_FUSION_TEL_STALE   (1u << 2u)

typedef enum {
    BM_EST_FUSION_COMPLEMENTARY = 0,
    BM_EST_FUSION_MAHONY
    /** @deprecated 占位枚举，validate 将拒绝；保留仅为 ABI 兼容 */
    , BM_EST_FUSION_EKF_CV
} bm_estimation_fusion_mode_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    roll_rad;
    float    pitch_rad;
    float    yaw_rad;
} bm_estimation_fusion_telemetry_t;

typedef int (*bm_estimation_fusion_read_imu_fn)(void *user,
                                                float *gx, float *gy, float *gz,
                                                float *ax, float *ay, float *az);

typedef void (*bm_estimation_fusion_publish_fn)(
    void *user,
    const bm_estimation_fusion_telemetry_t *telemetry);

typedef struct {
    bm_estimation_fusion_read_imu_fn read_imu;
    void                            *read_imu_user;
    bm_estimation_fusion_publish_fn  publish_telemetry;
    void                            *publish_telemetry_user;
} bm_estimation_fusion_resources_t;

typedef struct {
    bm_estimation_fusion_mode_t      mode;
    bm_algo_complementary_config_t   complementary;
    bm_algo_mahony_config_t          mahony;
    bm_algo_ekf_cv_config_t          ekf_cv;
    float                            dt_s;
} bm_estimation_fusion_config_t;

typedef struct {
    bm_algo_complementary_state_t complementary;
    bm_algo_mahony_state_t        mahony;
    bm_algo_ekf_cv_state_t        ekf_cv;
    bm_algo_euler_t               euler;
    uint32_t                      step_count;
    bm_estimation_fusion_telemetry_t telemetry;
} bm_estimation_fusion_state_t;

typedef struct {
    bm_estimation_fusion_config_t    config;
    bm_estimation_fusion_resources_t resources;
    bm_estimation_fusion_state_t     state;
} bm_estimation_fusion_axis_t;

int  bm_estimation_fusion_validate_config(
    const bm_estimation_fusion_config_t *config);
int  bm_estimation_fusion_init(bm_estimation_fusion_axis_t *axis);
void bm_estimation_fusion_reset(bm_estimation_fusion_axis_t *axis);
void bm_estimation_fusion_step(bm_estimation_fusion_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_ESTIMATION_FUSION_H */

/**
 * @file bm_algo_fusion.h
 * @brief 姿态融合：互补滤波、Mahony 与 Madgwick
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            增加 IMU 偏置/比例标定
 * 2026-06-23       1.2            zeh            NaN 拦截改用 bm_algo_is_finite_f；Mahony 积分项增加对称限幅
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_FUSION_H
#define BM_ALGO_FUSION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float roll_rad;
    float pitch_rad;
    float yaw_rad;
} bm_algo_euler_t;

typedef struct {
    float w;
    float x;
    float y;
    float z;
} bm_algo_quat_t;

/* ---------- 互补滤波（仅 roll/pitch） ---------- */
typedef struct {
    float alpha;  /**< 陀螺权重 */
} bm_algo_complementary_config_t;

typedef struct {
    float roll_rad;
    float pitch_rad;
} bm_algo_complementary_state_t;

void bm_algo_complementary_reset(bm_algo_complementary_state_t *state);
void bm_algo_complementary_step(bm_algo_complementary_state_t *state,
                                const bm_algo_complementary_config_t *config,
                                float gx, float gy, float gz,
                                float ax, float ay, float az,
                                float dt_s);

/* ---------- Mahony AHRS ---------- */
typedef struct {
    float kp;
    float ki;
} bm_algo_mahony_config_t;

typedef struct {
    bm_algo_quat_t q;
    float integral_x;
    float integral_y;
    float integral_z;
} bm_algo_mahony_state_t;

void bm_algo_mahony_reset(bm_algo_mahony_state_t *state);
void bm_algo_mahony_step(bm_algo_mahony_state_t *state,
                         const bm_algo_mahony_config_t *config,
                         float gx, float gy, float gz,
                         float ax, float ay, float az,
                         float dt_s);
void bm_algo_quat_to_euler(const bm_algo_quat_t *q, bm_algo_euler_t *euler);

/* ---------- Madgwick AHRS ---------- */
typedef struct {
    float beta;
} bm_algo_madgwick_config_t;

typedef struct {
    bm_algo_quat_t q;
} bm_algo_madgwick_state_t;

void bm_algo_madgwick_reset(bm_algo_madgwick_state_t *state);
void bm_algo_madgwick_step(bm_algo_madgwick_state_t *state,
                           const bm_algo_madgwick_config_t *config,
                           float gx, float gy, float gz,
                           float ax, float ay, float az,
                           float dt_s);

/* ---------- IMU 偏置/比例标定（E1 静态简化） ---------- */
typedef struct {
    float gyro_bias[3];
    float accel_bias[3];
    float gyro_scale[3];
    float accel_scale[3];
} bm_algo_imu_calib_config_t;

/**
 * @brief 应用 IMU 标定：out = scale * (raw - bias)
 */
void bm_algo_imu_calib_apply(const bm_algo_imu_calib_config_t *config,
                             const float raw_gyro[3],
                             const float raw_accel[3],
                             float out_gyro[3],
                             float out_accel[3]);

typedef struct {
    float gyro_sum[3];
    float accel_sum[3];
    uint32_t sample_count;
} bm_algo_imu_calib_accumulator_t;

void bm_algo_imu_calib_accumulator_reset(bm_algo_imu_calib_accumulator_t *acc);
int bm_algo_imu_calib_accumulator_feed(bm_algo_imu_calib_accumulator_t *acc,
                                       const float raw_gyro[3],
                                       const float raw_accel[3]);

/**
 * @brief 由静态样本均值估计 bias（scale 置 1）
 *
 * @param expected_accel 当前姿态下期望比力（如静止 Z 向上为 {0,0,9.81}）
 */
int bm_algo_imu_calib_accumulator_finish(
    const bm_algo_imu_calib_accumulator_t *acc,
    const float expected_accel[3],
    bm_algo_imu_calib_config_t *out_config);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FUSION_H */

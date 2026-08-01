/**
 * @file bm_algo_fusion.h
 * @brief 姿态融合：互补滤波、Mahony 与 Madgwick
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            增加 IMU 偏置/比例标定
 * 2026-06-23       1.2            zeh            NaN 拦截改用 bm_algo_is_finite_f；Mahony 积分项增加对称限幅
 * 2026-07-09       1.3            zeh            H10：Mahony/Madgwick 补陀螺
 *                                                gx/gy/gz 有限性校验，非有限
 *                                                则跳过本次积分，避免污染
 *                                                四元数持久状态
 * 2026-07-13       1.4            zeh            C4：complementary 补 gx/gy/
 *                                                ax/ay/az/alpha 有限性护栏；
 *                                                三滤波器 dt_s 补 NaN 拦截
 *
 * 2026-07-28       1.5            zeh            Calibration status documents BM_OK/BM_ERR_*
 * 2026-08-01       1.5            Codex          补齐公共 API 中文 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_FUSION_H
#define BM_ALGO_FUSION_H

#include "bm/algorithm/bm_algo_errors.h"

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

/**
 * @brief 复位互补姿态滤波器状态。
 * @param state 算法状态对象。
 */
void bm_algo_complementary_reset(bm_algo_complementary_state_t *state);
/**
 * @brief 执行一次互补姿态滤波器更新。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param gx 陀螺仪 X 轴角速度，单位 rad/s。
 * @param gy 陀螺仪 Y 轴角速度，单位 rad/s。
 * @param gz 陀螺仪 Z 轴角速度，单位 rad/s。
 * @param ax 加速度计 X 轴测量值。
 * @param ay 加速度计 Y 轴测量值。
 * @param az 加速度计 Z 轴测量值。
 * @param dt_s 本次更新的时间间隔，单位 s。
 */
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

/**
 * @brief 复位 Mahony 姿态滤波器状态。
 * @param state 算法状态对象。
 */
void bm_algo_mahony_reset(bm_algo_mahony_state_t *state);
/**
 * @brief 执行一次 Mahony 姿态滤波器更新。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param gx 陀螺仪 X 轴角速度，单位 rad/s。
 * @param gy 陀螺仪 Y 轴角速度，单位 rad/s。
 * @param gz 陀螺仪 Z 轴角速度，单位 rad/s。
 * @param ax 加速度计 X 轴测量值。
 * @param ay 加速度计 Y 轴测量值。
 * @param az 加速度计 Z 轴测量值。
 * @param dt_s 本次更新的时间间隔，单位 s。
 */
void bm_algo_mahony_step(bm_algo_mahony_state_t *state,
                         const bm_algo_mahony_config_t *config,
                         float gx, float gy, float gz,
                         float ax, float ay, float az,
                         float dt_s);
/**
 * @brief 将归一化四元数转换为欧拉角。
 * @param q 输出的瞬时正交功率分量。
 * @param euler 输出的欧拉角。
 */
void bm_algo_quat_to_euler(const bm_algo_quat_t *q, bm_algo_euler_t *euler);

/* ---------- Madgwick AHRS ---------- */
typedef struct {
    float beta;
} bm_algo_madgwick_config_t;

typedef struct {
    bm_algo_quat_t q;
} bm_algo_madgwick_state_t;

/**
 * @brief 复位 Madgwick 姿态滤波器状态。
 * @param state 算法状态对象。
 */
void bm_algo_madgwick_reset(bm_algo_madgwick_state_t *state);
/**
 * @brief 执行一次 Madgwick 姿态滤波器更新。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param gx 陀螺仪 X 轴角速度，单位 rad/s。
 * @param gy 陀螺仪 Y 轴角速度，单位 rad/s。
 * @param gz 陀螺仪 Z 轴角速度，单位 rad/s。
 * @param ax 加速度计 X 轴测量值。
 * @param ay 加速度计 Y 轴测量值。
 * @param az 加速度计 Z 轴测量值。
 * @param dt_s 本次更新的时间间隔，单位 s。
 */
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
 *
 * @param config 算法配置参数。
 * @param raw_gyro 三轴陀螺仪原始数据。
 * @param raw_accel 三轴加速度计原始数据。
 * @param out_gyro 输出的三轴已标定陀螺仪数据。
 * @param out_accel 输出的三轴已标定加速度计数据。
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

/**
 * @brief 清空 IMU 静态标定样本累加器。
 * @param acc 静态标定样本累加器。
 */
void bm_algo_imu_calib_accumulator_reset(bm_algo_imu_calib_accumulator_t *acc);
/**
 * @brief 向 IMU 静态标定累加器记录一组样本。
 * @return 成功返回 BM_OK；参数、配置或缓冲区无效时返回 BM_ERR_INVALID。
 *
 * @param acc 静态标定样本累加器。
 * @param raw_gyro 三轴陀螺仪原始数据。
 * @param raw_accel 三轴加速度计原始数据。
 */
int bm_algo_imu_calib_accumulator_feed(bm_algo_imu_calib_accumulator_t *acc,
                                       const float raw_gyro[3],
                                       const float raw_accel[3]);

/**
 * @brief 由静态样本均值估计 bias（scale 置 1）
 *
 * @param expected_accel 当前姿态下期望比力（如静止 Z 向上为 {0,0,9.81}）
 *
 * @param acc 静态标定样本累加器。
 * @param out_config 输出的 IMU 标定配置。
 * @return 成功返回 BM_OK；参数、配置或缓冲区无效时返回 BM_ERR_INVALID。
 */
int bm_algo_imu_calib_accumulator_finish(
    const bm_algo_imu_calib_accumulator_t *acc,
    const float expected_accel[3],
    bm_algo_imu_calib_config_t *out_config);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FUSION_H */

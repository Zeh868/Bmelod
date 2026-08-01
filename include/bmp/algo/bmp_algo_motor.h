/**
 * @file bmp_algo_motor.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief K2 · 闭源 · 需 bm_mp 的电机速度观测器（一维卡尔曼平滑）
 * @maturity E1
 * @author Bmelod contributors
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 * Date       Version Author Description
 * 2026-08-01 1.0     Codex  补齐规范化文件头元数据
 */
#ifndef BMP_ALGO_MOTOR_H
#define BMP_ALGO_MOTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float process_noise;
    float measure_noise;
} bmp_motor_config_t;

typedef struct {
    float speed_est;
    float covariance;
    uint8_t initialized;
    uint8_t reserved[3];
} bmp_motor_state_t;

/**
 * @brief 初始化电机速度观测器状态
 * @param state 电机速度观测器状态
 * @param config 电机速度观测器配置
 * @param speed_init 初始转速估计值
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bmp_motor_observer_init(bmp_motor_state_t *state,
                            const bmp_motor_config_t *config,
                            float speed_init);

/**
 * @brief 执行一步电机速度观测
 * @param state 电机速度观测器状态
 * @param config 电机速度观测器配置
 * @param speed_meas 当前转速测量值
 * @param dt_s 本次更新的时间间隔，单位 s
 * @param speed_est_out 输出的滤波转速估计值
 * @return BM_OK 成功；BM_ERR_INVALID 参数或状态无效
 */
int bmp_motor_observer_step(bmp_motor_state_t *state,
                            const bmp_motor_config_t *config,
                            float speed_meas,
                            float dt_s,
                            float *speed_est_out);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_MOTOR_H */

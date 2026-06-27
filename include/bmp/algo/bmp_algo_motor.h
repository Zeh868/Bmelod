/**
 * @file bmp_algo_motor.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief K2 · 闭源 · 需 bm_mp 的电机速度观测器（一维卡尔曼平滑）
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

int bmp_motor_observer_init(bmp_motor_state_t *state,
                            const bmp_motor_config_t *config,
                            float speed_init);

int bmp_motor_observer_step(bmp_motor_state_t *state,
                            const bmp_motor_config_t *config,
                            float speed_meas,
                            float dt_s,
                            float *speed_est_out);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_MOTOR_H */

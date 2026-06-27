/**
 * @file bmp_algo_motor.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief 电机速度观测器实现
 */
#include "bmp/algo/bmp_algo_motor.h"

#include "bm/algorithm/bm_algo_estimator.h"

#include <string.h>

int bmp_motor_observer_init(bmp_motor_state_t *state,
                            const bmp_motor_config_t *config,
                            float speed_init) {
    if (state == NULL || config == NULL) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    state->speed_est = speed_init;
    state->covariance = 1.0f;
    state->initialized = 1u;
    return 0;
}

int bmp_motor_observer_step(bmp_motor_state_t *state,
                            const bmp_motor_config_t *config,
                            float speed_meas,
                            float dt_s,
                            float *speed_est_out) {
    bm_algo_kalman1d_config_t kf_cfg;
    bm_algo_kalman1d_state_t kf_st;

    (void)dt_s;
    if (state == NULL || config == NULL || speed_est_out == NULL ||
        state->initialized == 0u) {
        return -1;
    }
    kf_cfg.q = config->process_noise;
    kf_cfg.r = config->measure_noise;
    kf_st.x = state->speed_est;
    kf_st.p = state->covariance;
    (void)bm_algo_kalman1d_predict(&kf_st, &kf_cfg);
    state->speed_est = bm_algo_kalman1d_update(&kf_st, &kf_cfg, speed_meas);
    state->covariance = kf_st.p;
    *speed_est_out = state->speed_est;
    return 0;
}

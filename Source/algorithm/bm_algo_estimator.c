/**
 * @file bm_algo_estimator.c
 * @brief 固定维度状态估算实现
 *
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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_estimator.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

#define BM_UKF1D_ALPHA 1.0f
#define BM_UKF1D_BETA  2.0f
#define BM_UKF1D_KAPPA 0.0f

/**
 * @brief 对称化并保护 1D 协方差
 */
static void ukf1d_sanitize_p(float *p) {
    if (p == NULL || !bm_algo_is_finite_f(*p) || *p < 0.0f) {
        if (p != NULL) {
            *p = 0.0f;
        }
    }
}

/**
 * @brief 固定测量模型 h(x)
 */
static float ukf1d_measure(float x, bm_algo_ukf1d_measurement_model_t model) {
    switch (model) {
    case BM_ALGO_UKF1D_MEAS_SQUARE:
        return x * x;
    default:
        return x;
    }
}

void bm_algo_kalman1d_reset(bm_algo_kalman1d_state_t *state,
                            float x0,
                            float p0) {
    if (state != NULL) {
        state->x = x0;
        state->p = (p0 >= 0.0f && bm_algo_is_finite_f(p0)) ? p0 : 0.0f;
    }
}

float bm_algo_kalman1d_predict(bm_algo_kalman1d_state_t *state,
                               const bm_algo_kalman1d_config_t *config) {
    if (state == NULL || config == NULL) {
        return 0.0f;
    }
    if (config->q >= 0.0f && bm_algo_is_finite_f(config->q)) {
        state->p += config->q;
    }
    return state->x;
}

float bm_algo_kalman1d_update(bm_algo_kalman1d_state_t *state,
                              const bm_algo_kalman1d_config_t *config,
                              float measurement) {
    float k;
    float denom;

    if (state == NULL || config == NULL) {
        return measurement;
    }

    if (!bm_algo_is_finite_f(measurement) || config->r < 0.0f ||
        !bm_algo_is_finite_f(config->r)) {
        return state->x;
    }
    denom = state->p + config->r;
    /* 阈值放宽至 1e-9f：原值 1e-12f 在 r=0 且 P 趋零时易误拒更新；
     * 配置校验应保证 r > 0，此处作为最后防线。 */
    if (denom <= 1e-9f || !bm_algo_is_finite_f(denom)) {
        return state->x;
    }
    k = state->p / denom;
    state->x += k * (measurement - state->x);
    state->p *= (1.0f - k);
    return state->x;
}

void bm_algo_ekf_cv_reset(bm_algo_ekf_cv_state_t *state, float pos, float vel) {
    if (state != NULL) {
        state->pos = pos;
        state->vel = vel;
        state->p00 = 1.0f;
        state->p01 = 0.0f;
        state->p10 = 0.0f;
        state->p11 = 1.0f;
    }
}

void bm_algo_ekf_cv_predict(bm_algo_ekf_cv_state_t *state,
                            const bm_algo_ekf_cv_config_t *config,
                            float dt_s) {
    float p00;
    float p01;
    float p10;
    float p11;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return;
    }

    state->pos += state->vel * dt_s;

    p00 = state->p00 + dt_s * (state->p10 + state->p01) + dt_s * dt_s * state->p11;
    p01 = state->p01 + dt_s * state->p11;
    p10 = state->p10 + dt_s * state->p11;
    p11 = state->p11;

    state->p00 = p00 + config->q_pos;
    state->p01 = p01;
    state->p10 = p10;
    state->p11 = p11 + config->q_vel;
}

void bm_algo_ekf_cv_update(bm_algo_ekf_cv_state_t *state,
                           const bm_algo_ekf_cv_config_t *config,
                           float pos_meas) {
    float s;
    float k0;
    float k1;
    float y;
    float p00;
    float p01;
    float p10;
    float p11;

    if (state == NULL || config == NULL) {
        return;
    }

    s = state->p00 + config->r_pos;
    if (s <= 0.0f) {
        return;
    }
    k0 = state->p00 / s;
    k1 = state->p10 / s;
    y = pos_meas - state->pos;
    p00 = state->p00;
    p01 = state->p01;
    p10 = state->p10;
    p11 = state->p11;

    state->pos += k0 * y;
    state->vel += k1 * y;

    state->p00 = p00 - k0 * p00;
    state->p01 = p01 - k0 * p01;
    state->p10 = p10 - k1 * p00;
    state->p11 = p11 - k1 * p01;
    state->p01 = 0.5f * (state->p01 + state->p10);
    state->p10 = state->p01;
}

void bm_algo_ukf1d_reset(bm_algo_ukf1d_state_t *state, float x0, float p0) {
    if (state == NULL) {
        return;
    }
    state->x = bm_algo_is_finite_f(x0) ? x0 : 0.0f;
    state->p = (p0 >= 0.0f && bm_algo_is_finite_f(p0)) ? p0 : 1.0f;
    ukf1d_sanitize_p(&state->p);
}

void bm_algo_ukf1d_predict(bm_algo_ukf1d_state_t *state,
                           const bm_algo_ukf1d_config_t *config) {
    if (state == NULL || config == NULL) {
        return;
    }
    if (!bm_algo_is_finite_f(state->x) || !bm_algo_is_finite_f(state->p)) {
        bm_algo_ukf1d_reset(state, 0.0f, 1.0f);
        return;
    }
    if (config->q >= 0.0f && bm_algo_is_finite_f(config->q)) {
        state->p += config->q;
    }
    ukf1d_sanitize_p(&state->p);
}

int bm_algo_ukf1d_update(bm_algo_ukf1d_state_t *state,
                         const bm_algo_ukf1d_config_t *config,
                         float measurement) {
    float n_lambda;
    float gamma;
    float sqrt_p;
    float sp[3];
    float zp[3];
    float w0;       /**< 0 号 sigma 点均值权 */
    float w0_cov;   /**< 0 号 sigma 点协方差权 = w0 + (1 - α² + β) */
    float w1;
    float z_mean;
    float pzz;
    float pxz;
    float k;
    float innov;
    uint32_t i;

    if (state == NULL || config == NULL) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }
    if (!bm_algo_is_finite_f(measurement) || config->r < 0.0f ||
        !bm_algo_is_finite_f(config->r) || !bm_algo_is_finite_f(state->x) ||
        !bm_algo_is_finite_f(state->p)) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }

    n_lambda = BM_UKF1D_ALPHA * BM_UKF1D_ALPHA * (1.0f + BM_UKF1D_KAPPA);
    if (n_lambda <= 1e-12f) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }
    gamma = sqrtf(n_lambda);
    sqrt_p = sqrtf(state->p);
    sp[0] = state->x;
    sp[1] = state->x + gamma * sqrt_p;
    sp[2] = state->x - gamma * sqrt_p;

    w0 = (n_lambda - 1.0f) / n_lambda;
    w1 = 0.5f / n_lambda;
    /* w0_cov = w0_mean + (1 - α² + β)
     * 其中 α² = BM_UKF1D_ALPHA²，β = BM_UKF1D_BETA。
     * β=0、α=1 时修正量为 0，退化为原均值权，行为不变。 */
    w0_cov = w0 + (1.0f
                   - BM_UKF1D_ALPHA * BM_UKF1D_ALPHA
                   + BM_UKF1D_BETA);

    z_mean = 0.0f;
    pzz = 0.0f;
    pxz = 0.0f;
    for (i = 0u; i < 3u; ++i) {
        zp[i] = ukf1d_measure(sp[i], config->measurement_model);
        if (!bm_algo_is_finite_f(zp[i])) {
            return BM_ALGO_EKF_UPDATE_INVALID;
        }
    }

    z_mean = w0 * zp[0] + w1 * (zp[1] + zp[2]);
    for (i = 0u; i < 3u; ++i) {
        float dz      = zp[i] - z_mean;
        float dx      = sp[i] - state->x;
        float w_mean  = (i == 0u) ? w0     : w1; /**< 均值权，用于 pxz */
        float w_c     = (i == 0u) ? w0_cov : w1; /**< 协方差权，用于 pzz */
        pzz += w_c    * dz * dz;
        pxz += w_mean * dx * dz;
    }
    pzz += config->r;
    if (pzz <= 1e-12f || !bm_algo_is_finite_f(pzz)) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }

    innov = measurement - z_mean;
    k = pxz / pzz;
    if (!bm_algo_is_finite_f(k)) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }

    state->x += k * innov;
    state->p -= k * pzz * k;
    ukf1d_sanitize_p(&state->p);

    return BM_ALGO_EKF_UPDATE_OK;
}

int bm_algo_ekf_gate_accept(float innovation,
                            float innovation_var,
                            float threshold) {
    float nis;

    if (innovation_var <= 0.0f || threshold <= 0.0f ||
        !bm_algo_is_finite_f(innovation) ||
        !bm_algo_is_finite_f(innovation_var) ||
        !bm_algo_is_finite_f(threshold)) {
        return 0;
    }
    nis = (innovation * innovation) / innovation_var;
    return (nis <= threshold) ? 1 : 0;
}

int bm_algo_ekf_cv_update_gated(bm_algo_ekf_cv_state_t *state,
                                const bm_algo_ekf_cv_config_t *config,
                                float pos_meas,
                                const bm_algo_ekf_gate_config_t *gate) {
    float s;
    float innov;

    if (state == NULL || config == NULL) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }
    if (!bm_algo_is_finite_f(pos_meas)) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }

    s = state->p00 + config->r_pos;
    if (s <= 0.0f || !bm_algo_is_finite_f(s)) {
        return BM_ALGO_EKF_UPDATE_INVALID;
    }

    innov = pos_meas - state->pos;
    if (gate != NULL &&
        !bm_algo_ekf_gate_accept(innov, s, gate->innovation_threshold)) {
        return BM_ALGO_EKF_UPDATE_GATED;
    }

    bm_algo_ekf_cv_update(state, config, pos_meas);
    return BM_ALGO_EKF_UPDATE_OK;
}

/**
 * @file bmp_algo_motor_ekf.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief 高级无感观测器 (EKF) 实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 */
#include "bmp/algo/bmp_algo_motor_ekf.h"

#include "bm/algorithm/bm_algo_common.h"

#include <math.h>
#include <string.h>

/** 过程噪声缺省值（config->q_covariance <= 0 时） */
#define BMP_MOTOR_EKF_Q_DEFAULT  1e-4f
/** 测量噪声缺省值（config->r_covariance <= 0 时） */
#define BMP_MOTOR_EKF_R_DEFAULT  1e-3f

/**
 * @brief 4x4 矩阵乘法 out = a * b
 */
static void bmp_motor_ekf_mat4_mul(float out[4][4],
                                   const float a[4][4],
                                   const float b[4][4]) {
    uint8_t i;
    uint8_t j;
    uint8_t k;

    for (i = 0u; i < 4u; ++i) {
        for (j = 0u; j < 4u; ++j) {
            float sum = 0.0f;
            for (k = 0u; k < 4u; ++k) {
                sum += a[i][k] * b[k][j];
            }
            out[i][j] = sum;
        }
    }
}

/**
 * @brief 4x4 矩阵乘转置 out = a * b^T
 */
static void bmp_motor_ekf_mat4_mul_at(float out[4][4],
                                      const float a[4][4],
                                      const float b[4][4]) {
    uint8_t i;
    uint8_t j;
    uint8_t k;

    for (i = 0u; i < 4u; ++i) {
        for (j = 0u; j < 4u; ++j) {
            float sum = 0.0f;
            for (k = 0u; k < 4u; ++k) {
                sum += a[i][k] * b[j][k];
            }
            out[i][j] = sum;
        }
    }
}

/**
 * @brief 强制 4x4 协方差矩阵对称
 */
static void bmp_motor_ekf_symmetrize_p(float p[4][4]) {
    uint8_t i;
    uint8_t j;

    for (i = 0u; i < 4u; ++i) {
        for (j = (uint8_t)(i + 1u); j < 4u; ++j) {
            float avg = 0.5f * (p[i][j] + p[j][i]);
            p[i][j] = avg;
            p[j][i] = avg;
        }
    }
}

/**
 * @brief EKF 协方差预测 P = F P F^T + Q
 */
static void bmp_motor_ekf_predict_covariance(float p[4][4],
                                             float rs,
                                             float inv_ls,
                                             float omega_rad_s,
                                             float dt_s,
                                             float q_diag) {
    float f[4][4];
    float fp[4][4];
    float f00;
    float f02;
    float f23;
    float f32;
    uint8_t i;

    f00 = 1.0f - dt_s * rs * inv_ls;
    f02 = -dt_s * inv_ls;
    f23 = -dt_s * omega_rad_s;
    f32 = dt_s * omega_rad_s;

    memset(f, 0, sizeof(f));
    f[0][0] = f00;
    f[1][1] = f00;
    f[0][2] = f02;
    f[1][3] = f02;
    f[2][2] = 1.0f;
    f[3][3] = 1.0f;
    f[2][3] = f23;
    f[3][2] = f32;

    bmp_motor_ekf_mat4_mul(fp, f, p);
    bmp_motor_ekf_mat4_mul_at(p, fp, f);

    for (i = 0u; i < 4u; ++i) {
        p[i][i] += q_diag;
    }
    bmp_motor_ekf_symmetrize_p(p);
}

/**
 * @brief EKF 测量更新，输出卡尔曼增益并更新协方差
 */
static void bmp_motor_ekf_update_covariance(float p[4][4],
                                          float k[4][2],
                                          float r_diag) {
    float s00;
    float s01;
    float s11;
    float det;
    float inv_det;
    float invs00;
    float invs01;
    float invs11;
    float p00;
    float p01;
    float p02;
    float p03;
    float p10;
    float p11;
    float p12;
    float p13;
    float p20;
    float p21;
    float p22;
    float p23;
    float p30;
    float p31;
    float p32;
    float p33;
    uint8_t i;

    s00 = p[0][0] + r_diag;
    s01 = p[0][1];
    s11 = p[1][1] + r_diag;
    det = s00 * s11 - s01 * s01;
    if (det <= 1e-12f || !bm_algo_is_finite_f(det)) {
        memset(k, 0, sizeof(float) * 4u * 2u);
        return;
    }

    inv_det = 1.0f / det;
    invs00 = s11 * inv_det;
    invs01 = -s01 * inv_det;
    invs11 = s00 * inv_det;

    for (i = 0u; i < 4u; ++i) {
        k[i][0] = p[i][0] * invs00 + p[i][1] * invs01;
        k[i][1] = p[i][0] * invs01 + p[i][1] * invs11;
    }

    p00 = p[0][0]; p01 = p[0][1]; p02 = p[0][2]; p03 = p[0][3];
    p10 = p[1][0]; p11 = p[1][1]; p12 = p[1][2]; p13 = p[1][3];
    p20 = p[2][0]; p21 = p[2][1]; p22 = p[2][2]; p23 = p[2][3];
    p30 = p[3][0]; p31 = p[3][1]; p32 = p[3][2]; p33 = p[3][3];

    p[0][0] = p00 - k[0][0] * p00 - k[0][1] * p10;
    p[0][1] = p01 - k[0][0] * p01 - k[0][1] * p11;
    p[0][2] = p02 - k[0][0] * p02 - k[0][1] * p12;
    p[0][3] = p03 - k[0][0] * p03 - k[0][1] * p13;

    p[1][0] = p10 - k[1][0] * p00 - k[1][1] * p10;
    p[1][1] = p11 - k[1][0] * p01 - k[1][1] * p11;
    p[1][2] = p12 - k[1][0] * p02 - k[1][1] * p12;
    p[1][3] = p13 - k[1][0] * p03 - k[1][1] * p13;

    p[2][0] = p20 - k[2][0] * p00 - k[2][1] * p10;
    p[2][1] = p21 - k[2][0] * p01 - k[2][1] * p11;
    p[2][2] = p22 - k[2][0] * p02 - k[2][1] * p12;
    p[2][3] = p23 - k[2][0] * p03 - k[2][1] * p13;

    p[3][0] = p30 - k[3][0] * p00 - k[3][1] * p10;
    p[3][1] = p31 - k[3][0] * p01 - k[3][1] * p11;
    p[3][2] = p32 - k[3][0] * p02 - k[3][1] * p12;
    p[3][3] = p33 - k[3][0] * p03 - k[3][1] * p13;

    bmp_motor_ekf_symmetrize_p(p);
}

int bmp_motor_ekf_init(bmp_motor_ekf_state_t *state,
                       const bmp_motor_ekf_config_t *config) {
    if (state == NULL || config == NULL || config->ls_h <= 0.0f) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    state->p[0][0] = 1.0f;
    state->p[1][1] = 1.0f;
    state->p[2][2] = 1.0f;
    state->p[3][3] = 1.0f;
    state->initialized = 1u;
    return 0;
}

int bmp_motor_ekf_step(bmp_motor_ekf_state_t *state,
                       const bmp_motor_ekf_config_t *config,
                       float v_alpha,
                       float v_beta,
                       float i_alpha_meas,
                       float i_beta_meas,
                       float dt_s) {
    float ialpha;
    float ibeta;
    float ealpha;
    float ebeta;
    float rs;
    float ls;
    float inv_ls;
    float q_diag;
    float r_diag;
    float ialpha_pred;
    float ibeta_pred;
    float ealpha_pred;
    float ebeta_pred;
    float err_ia;
    float err_ib;
    float k[4][2];
    float emf_mag;
    float speed;
    float cross_prod;

    if (state == NULL || config == NULL || state->initialized == 0u || dt_s <= 0.0f) {
        return -1;
    }

    rs = config->rs_ohm;
    ls = config->ls_h;
    inv_ls = 1.0f / ls;
    q_diag = (config->q_covariance > 0.0f) ?
             config->q_covariance : BMP_MOTOR_EKF_Q_DEFAULT;
    r_diag = (config->r_covariance > 0.0f) ?
             config->r_covariance : BMP_MOTOR_EKF_R_DEFAULT;

    ialpha = state->i_alpha;
    ibeta = state->i_beta;
    ealpha = state->emf_alpha;
    ebeta = state->emf_beta;

    /* 1. Predict state */
    ialpha_pred = ialpha + dt_s * (inv_ls * (v_alpha - rs * ialpha - ealpha));
    ibeta_pred = ibeta + dt_s * (inv_ls * (v_beta - rs * ibeta - ebeta));
    ealpha_pred = ealpha - dt_s * state->omega_rad_s * ebeta;
    ebeta_pred = ebeta + dt_s * state->omega_rad_s * ealpha;

    /* 2. Predict covariance */
    bmp_motor_ekf_predict_covariance(state->p, rs, inv_ls,
                                     state->omega_rad_s, dt_s, q_diag);

    /* 3. Measurement update */
    bmp_motor_ekf_update_covariance(state->p, k, r_diag);

    err_ia = i_alpha_meas - ialpha_pred;
    err_ib = i_beta_meas - ibeta_pred;

    /* 4. Update state */
    state->i_alpha = ialpha_pred + k[0][0] * err_ia + k[0][1] * err_ib;
    state->i_beta = ibeta_pred + k[1][0] * err_ia + k[1][1] * err_ib;
    state->emf_alpha = ealpha_pred + k[2][0] * err_ia + k[2][1] * err_ib;
    state->emf_beta = ebeta_pred + k[3][0] * err_ia + k[3][1] * err_ib;

    /* 5. Extract angle and speed */
    state->theta_rad = atan2f(-state->emf_alpha, state->emf_beta);
    state->theta_rad = bm_algo_angle_wrap_rad(state->theta_rad);

    emf_mag = sqrtf(state->emf_alpha * state->emf_alpha +
                    state->emf_beta * state->emf_beta);
    if (config->ke_v_rad_s > 0.0f) {
        speed = emf_mag / config->ke_v_rad_s;
        cross_prod = state->emf_alpha * ebeta_pred - state->emf_beta * ealpha_pred;
        state->omega_rad_s = (cross_prod > 0.0f) ? speed : -speed;
    }

    return 0;
}

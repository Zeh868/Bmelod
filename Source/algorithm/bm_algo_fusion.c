/**
 * @file bm_algo_fusion.c
 * @brief 姿态融合算法实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            增加 IMU 偏置/比例标定
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_fusion.h"
#include <stddef.h>

#include <math.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

static float inv_sqrt(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    return 1.0f / sqrtf(x);
}

void bm_algo_complementary_reset(bm_algo_complementary_state_t *state) {
    if (state != NULL) {
        state->roll_rad = 0.0f;
        state->pitch_rad = 0.0f;
    }
}

void bm_algo_complementary_step(bm_algo_complementary_state_t *state,
                                const bm_algo_complementary_config_t *config,
                                float gx, float gy, float gz,
                                float ax, float ay, float az,
                                float dt_s) {
    float roll_acc;
    float pitch_acc;
    float alpha;

    (void)gz;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return;
    }

    roll_acc = atan2f(ay, az);
    pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az));

    state->roll_rad += gx * dt_s;
    state->pitch_rad += gy * dt_s;

    alpha = config->alpha;
    state->roll_rad = alpha * state->roll_rad + (1.0f - alpha) * roll_acc;
    state->pitch_rad = alpha * state->pitch_rad + (1.0f - alpha) * pitch_acc;
}

void bm_algo_mahony_reset(bm_algo_mahony_state_t *state) {
    if (state != NULL) {
        state->q.w = 1.0f;
        state->q.x = 0.0f;
        state->q.y = 0.0f;
        state->q.z = 0.0f;
        state->integral_x = 0.0f;
        state->integral_y = 0.0f;
        state->integral_z = 0.0f;
    }
}

void bm_algo_mahony_step(bm_algo_mahony_state_t *state,
                         const bm_algo_mahony_config_t *config,
                         float gx, float gy, float gz,
                         float ax, float ay, float az,
                         float dt_s) {
    float q0;
    float q1;
    float q2;
    float q3;
    float recip_norm;
    float vx;
    float vy;
    float vz;
    float ex;
    float ey;
    float ez;
    float q_dot0;
    float q_dot1;
    float q_dot2;
    float q_dot3;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return;
    }

    q0 = state->q.w;
    q1 = state->q.x;
    q2 = state->q.y;
    q3 = state->q.z;

    if (ax != 0.0f || ay != 0.0f || az != 0.0f) {
        recip_norm = inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        vx = 2.0f * (q1 * q3 - q0 * q2);
        vy = 2.0f * (q0 * q1 + q2 * q3);
        vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

        ex = ay * vz - az * vy;
        ey = az * vx - ax * vz;
        ez = ax * vy - ay * vx;

        state->integral_x += config->ki * ex * dt_s;
        state->integral_y += config->ki * ey * dt_s;
        state->integral_z += config->ki * ez * dt_s;

        gx += config->kp * ex + state->integral_x;
        gy += config->kp * ey + state->integral_y;
        gz += config->kp * ez + state->integral_z;
    }

    q_dot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    q_dot1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
    q_dot2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
    q_dot3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

    q0 += q_dot0 * dt_s;
    q1 += q_dot1 * dt_s;
    q2 += q_dot2 * dt_s;
    q3 += q_dot3 * dt_s;

    recip_norm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    state->q.w = q0 * recip_norm;
    state->q.x = q1 * recip_norm;
    state->q.y = q2 * recip_norm;
    state->q.z = q3 * recip_norm;
}

void bm_algo_madgwick_reset(bm_algo_madgwick_state_t *state) {
    if (state != NULL) {
        state->q.w = 1.0f;
        state->q.x = 0.0f;
        state->q.y = 0.0f;
        state->q.z = 0.0f;
    }
}

void bm_algo_madgwick_step(bm_algo_madgwick_state_t *state,
                           const bm_algo_madgwick_config_t *config,
                           float gx, float gy, float gz,
                           float ax, float ay, float az,
                           float dt_s) {
    float q0;
    float q1;
    float q2;
    float q3;
    float recip_norm;
    float s0;
    float s1;
    float s2;
    float s3;
    float q_dot0;
    float q_dot1;
    float q_dot2;
    float q_dot3;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return;
    }

    q0 = state->q.w;
    q1 = state->q.x;
    q2 = state->q.y;
    q3 = state->q.z;

    if (ax != 0.0f || ay != 0.0f || az != 0.0f) {
        recip_norm = inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recip_norm;
        ay *= recip_norm;
        az *= recip_norm;

        s0 = -2.0f * q2 * (2.0f * q1 * q3 - 2.0f * q0 * q2 - ax)
             + 2.0f * q1 * (2.0f * q0 * q1 + 2.0f * q2 * q3 - ay);
        s1 =  2.0f * q3 * (2.0f * q1 * q3 - 2.0f * q0 * q2 - ax)
             + 2.0f * q0 * (2.0f * q0 * q1 + 2.0f * q2 * q3 - ay)
             - 4.0f * q1 * (1.0f - 2.0f * q1 * q1 - 2.0f * q2 * q2 - az);
        s2 = -2.0f * q0 * (2.0f * q1 * q3 - 2.0f * q0 * q2 - ax)
             + 2.0f * q3 * (2.0f * q0 * q1 + 2.0f * q2 * q3 - ay)
             - 4.0f * q2 * (1.0f - 2.0f * q1 * q1 - 2.0f * q2 * q2 - az);
        s3 =  2.0f * q1 * (2.0f * q1 * q3 - 2.0f * q0 * q2 - ax)
             + 2.0f * q2 * (2.0f * q0 * q1 + 2.0f * q2 * q3 - ay);

        recip_norm = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        s0 *= recip_norm;
        s1 *= recip_norm;
        s2 *= recip_norm;
        s3 *= recip_norm;

        q_dot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - config->beta * s0;
        q_dot1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy) - config->beta * s1;
        q_dot2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx) - config->beta * s2;
        q_dot3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx) - config->beta * s3;
    } else {
        q_dot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        q_dot1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
        q_dot2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
        q_dot3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);
    }

    q0 += q_dot0 * dt_s;
    q1 += q_dot1 * dt_s;
    q2 += q_dot2 * dt_s;
    q3 += q_dot3 * dt_s;

    recip_norm = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    state->q.w = q0 * recip_norm;
    state->q.x = q1 * recip_norm;
    state->q.y = q2 * recip_norm;
    state->q.z = q3 * recip_norm;
}

void bm_algo_quat_to_euler(const bm_algo_quat_t *q, bm_algo_euler_t *euler) {
    float sinr;
    float cosr;
    float sinp;
    float siny;
    float cosy;

    if (q == NULL || euler == NULL) {
        return;
    }

    sinr = 2.0f * (q->w * q->x + q->y * q->z);
    cosr = 1.0f - 2.0f * (q->x * q->x + q->y * q->y);
    euler->roll_rad = atan2f(sinr, cosr);

    sinp = 2.0f * (q->w * q->y - q->z * q->x);
    if (fabsf(sinp) >= 1.0f) {
        euler->pitch_rad = (sinp > 0.0f) ? 1.5707963f : -1.5707963f;
    } else {
        euler->pitch_rad = asinf(sinp);
    }

    siny = 2.0f * (q->w * q->z + q->x * q->y);
    cosy = 1.0f - 2.0f * (q->y * q->y + q->z * q->z);
    euler->yaw_rad = atan2f(siny, cosy);
}

void bm_algo_imu_calib_apply(const bm_algo_imu_calib_config_t *config,
                             const float raw_gyro[3],
                             const float raw_accel[3],
                             float out_gyro[3],
                             float out_accel[3]) {
    uint32_t i;

    if (config == NULL || raw_gyro == NULL || raw_accel == NULL) {
        return;
    }
    for (i = 0u; i < 3u; ++i) {
        float g = raw_gyro[i] - config->gyro_bias[i];
        float a = raw_accel[i] - config->accel_bias[i];
        if (out_gyro != NULL) {
            out_gyro[i] = g * config->gyro_scale[i];
        }
        if (out_accel != NULL) {
            out_accel[i] = a * config->accel_scale[i];
        }
    }
}

void bm_algo_imu_calib_accumulator_reset(bm_algo_imu_calib_accumulator_t *acc) {
    if (acc != NULL) {
        acc->gyro_sum[0] = 0.0f;
        acc->gyro_sum[1] = 0.0f;
        acc->gyro_sum[2] = 0.0f;
        acc->accel_sum[0] = 0.0f;
        acc->accel_sum[1] = 0.0f;
        acc->accel_sum[2] = 0.0f;
        acc->sample_count = 0u;
    }
}

int bm_algo_imu_calib_accumulator_feed(bm_algo_imu_calib_accumulator_t *acc,
                                       const float raw_gyro[3],
                                       const float raw_accel[3]) {
    uint32_t i;

    if (acc == NULL || raw_gyro == NULL || raw_accel == NULL) {
        return -1;
    }
    for (i = 0u; i < 3u; ++i) {
        if (!isfinite(raw_gyro[i]) || !isfinite(raw_accel[i])) {
            return -1;
        }
        acc->gyro_sum[i] += raw_gyro[i];
        acc->accel_sum[i] += raw_accel[i];
    }
    acc->sample_count++;
    return 0;
}

int bm_algo_imu_calib_accumulator_finish(
    const bm_algo_imu_calib_accumulator_t *acc,
    const float expected_accel[3],
    bm_algo_imu_calib_config_t *out_config) {
    float inv_n;
    uint32_t i;

    if (acc == NULL || expected_accel == NULL || out_config == NULL ||
        acc->sample_count == 0u) {
        return -1;
    }
    inv_n = 1.0f / (float)acc->sample_count;
    for (i = 0u; i < 3u; ++i) {
        out_config->gyro_bias[i] = acc->gyro_sum[i] * inv_n;
        out_config->accel_bias[i] = acc->accel_sum[i] * inv_n - expected_accel[i];
        out_config->gyro_scale[i] = 1.0f;
        out_config->accel_scale[i] = 1.0f;
    }
    return 0;
}

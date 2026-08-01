/**
 * @file bm_algo_fusion.c
 * @brief 姿态融合算法实现
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.7
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.7            Codex           补齐 static 辅助函数 Doxygen
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            增加 IMU 偏置/比例标定
 * 2026-06-23       1.2            zeh            NaN 拦截改用 bm_algo_is_finite_f；Mahony 积分项增加对称限幅
 * 2026-07-09       1.3            zeh            H10：Mahony/Madgwick 补陀螺
 *                                                gx/gy/gz 有限性校验，非有限
 *                                                则跳过本次积分，避免污染
 *                                                四元数持久状态
 * 2026-07-13       1.4            zeh            C4：complementary 补 gx/gy/
 *                                                ax/ay/az/alpha 有限性护栏
 *                                                （H10 遗漏的第三个滤波器）；
 *                                                三滤波器 dt_s 补 NaN 拦截
 * 2026-07-16       1.5            zeh            imu_calib_accumulator_feed 改先校验
 *                                                后提交：三轴任一非有限即整帧拒绝，
 *                                                消除中途失败的半更新状态
 * 2026-07-27       1.6            zeh            complementary_step 的 alpha
 *                                                改用 bm_algo_lpf1_alpha_saturate
 * 2026-07-28       1.7            zeh            状态返回改用 BM_OK/BM_ERR_*
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_fusion.h"
#include "bm/algorithm/bm_algo_errors.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

/**
 * @brief Mahony 积分项对称限幅上界（rad/s²·s 等效）
 *
 * 限幅依据：kp 典型值 2.0，ki 典型值 0.005；在 ki 最大 0.1、最差误差 π rad 且
 * 连续 100 s 积分的极端场景下积分项上界约为 31.4。取 50 留有余量，足以覆盖
 * 实际 AHRS 工作范围，同时防止积分无界发散。
 */
#define BM_MAHONY_INTEGRAL_LIMIT 50.0f

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

/** π/2（万向锁时 pitch 饱和至 ±90°）；保持原字面量值不变 */
#ifndef BM_ALGO_HALF_PI_F
#define BM_ALGO_HALF_PI_F 1.5707963f
#endif

/**
 * @brief 计算非负输入的平方根倒数
 *
 * @param x 输入值
 * @return x 大于 0 时返回 1/sqrt(x)；x 小于等于 0 时返回 0
 */
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
    /* C4（H10 同款护栏）：gx/gy 直接积分进 roll_rad/pitch_rad 持久状态，
     * ax/ay/az 经 atan2f 参与互补混合，config->alpha 逐项相乘，dt_s 为
     * NaN 时可穿过上方 <=0 判断——任一非有限输入一次即可永久污染姿态
     * 状态且无法自愈。非有限则跳过本次积分，保持上一次有限估计不变。
     * gz 未参与运算（(void)gz），不做校验以免误拒可用样本。 */
    if (!bm_algo_is_finite_f(dt_s) ||
        !bm_algo_is_finite_f(gx) || !bm_algo_is_finite_f(gy) ||
        !bm_algo_is_finite_f(ax) || !bm_algo_is_finite_f(ay) ||
        !bm_algo_is_finite_f(az) || !bm_algo_is_finite_f(config->alpha)) {
        return;
    }

    roll_acc = atan2f(ay, az);
    pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az));

    state->roll_rad += gx * dt_s;
    state->pitch_rad += gy * dt_s;

    alpha = bm_algo_lpf1_alpha_saturate(config->alpha);
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

    /* dt_s 为 NaN 时 <=0 比较恒 false 会放行，经 q += q_dot*dt 污染四元数，
     * 补有限性校验（与 ekf_cv_predict 的 H9 护栏纪律对齐） */
    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        !bm_algo_is_finite_f(dt_s)) {
        return;
    }
    /* 配置增益 kp/ki 非有限会经积分项污染四元数持久状态，此前无护栏 */
    if (!bm_algo_is_finite_f(config->kp) || !bm_algo_is_finite_f(config->ki)) {
        return;
    }
    /* H10：陀螺 gx/gy/gz 无论加速度计路径是否生效都直接参与 q_dot 计算，
     * 一旦为 NaN/Inf 会立即污染四元数持久状态；非有限则跳过本次积分，
     * 保持上一次有限的姿态估计不变。 */
    if (!bm_algo_is_finite_f(gx) || !bm_algo_is_finite_f(gy) ||
        !bm_algo_is_finite_f(gz)) {
        return;
    }

    q0 = state->q.w;
    q1 = state->q.x;
    q2 = state->q.y;
    q3 = state->q.z;

    /* 使用 bm_algo_is_finite_f 同时拦截 NaN 与 Inf；纯零向量跳过加速度修正 */
    if (bm_algo_is_finite_f(ax) && bm_algo_is_finite_f(ay) && bm_algo_is_finite_f(az) &&
        (ax != 0.0f || ay != 0.0f || az != 0.0f)) {
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

        /* 对称限幅：防止积分项在长时间低速或传感器故障时无界增长 */
        state->integral_x = bm_algo_clamp_f(state->integral_x,
                                            -BM_MAHONY_INTEGRAL_LIMIT,
                                             BM_MAHONY_INTEGRAL_LIMIT);
        state->integral_y = bm_algo_clamp_f(state->integral_y,
                                            -BM_MAHONY_INTEGRAL_LIMIT,
                                             BM_MAHONY_INTEGRAL_LIMIT);
        state->integral_z = bm_algo_clamp_f(state->integral_z,
                                            -BM_MAHONY_INTEGRAL_LIMIT,
                                             BM_MAHONY_INTEGRAL_LIMIT);

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

    /* dt_s 为 NaN 时 <=0 比较恒 false 会放行，经 q += q_dot*dt 污染四元数，
     * 补有限性校验（与 Mahony 路径一致） */
    if (state == NULL || config == NULL || dt_s <= 0.0f ||
        !bm_algo_is_finite_f(dt_s)) {
        return;
    }
    /* 配置增益 beta 非有限会经梯度修正项污染四元数持久状态，此前无护栏 */
    if (!bm_algo_is_finite_f(config->beta)) {
        return;
    }
    /* H10：陀螺 gx/gy/gz 无论加速度计路径是否生效都直接参与 q_dot 计算，
     * 一旦为 NaN/Inf 会立即污染四元数持久状态；非有限则跳过本次积分，
     * 保持上一次有限的姿态估计不变。 */
    if (!bm_algo_is_finite_f(gx) || !bm_algo_is_finite_f(gy) ||
        !bm_algo_is_finite_f(gz)) {
        return;
    }

    q0 = state->q.w;
    q1 = state->q.x;
    q2 = state->q.y;
    q3 = state->q.z;

    /* 使用 bm_algo_is_finite_f 同时拦截 NaN 与 Inf；纯零向量跳过梯度修正 */
    if (bm_algo_is_finite_f(ax) && bm_algo_is_finite_f(ay) && bm_algo_is_finite_f(az) &&
        (ax != 0.0f || ay != 0.0f || az != 0.0f)) {
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
        euler->pitch_rad = (sinp > 0.0f) ? BM_ALGO_HALF_PI_F : -BM_ALGO_HALF_PI_F;
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
        return BM_ERR_INVALID;
    }
    /* 先校验后提交：三轴全部有限才累加，避免中途失败时已写入的轴
     * 留下半更新状态（gyro_sum/accel_sum 已变而 sample_count 未增）。 */
    for (i = 0u; i < 3u; ++i) {
        if (!isfinite(raw_gyro[i]) || !isfinite(raw_accel[i])) {
            return BM_ERR_INVALID;
        }
    }
    for (i = 0u; i < 3u; ++i) {
        acc->gyro_sum[i] += raw_gyro[i];
        acc->accel_sum[i] += raw_accel[i];
    }
    acc->sample_count++;
    return BM_OK;
}

int bm_algo_imu_calib_accumulator_finish(
    const bm_algo_imu_calib_accumulator_t *acc,
    const float expected_accel[3],
    bm_algo_imu_calib_config_t *out_config) {
    float inv_n;
    uint32_t i;

    if (acc == NULL || expected_accel == NULL || out_config == NULL ||
        acc->sample_count == 0u) {
        return BM_ERR_INVALID;
    }
    inv_n = 1.0f / (float)acc->sample_count;
    for (i = 0u; i < 3u; ++i) {
        out_config->gyro_bias[i] = acc->gyro_sum[i] * inv_n;
        out_config->accel_bias[i] = acc->accel_sum[i] * inv_n - expected_accel[i];
        out_config->gyro_scale[i] = 1.0f;
        out_config->accel_scale[i] = 1.0f;
    }
    return BM_OK;
}

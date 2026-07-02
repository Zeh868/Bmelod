/**
 * @file estimation_fusion.c
 * @brief 估算融合选择器组件实现
 *
 * 支持互补滤波、Mahony AHRS、EKF-CV 单轴 pitch 跟踪三种模式，
 * 并提供 bm_exec_ops_t 调度封装（run 驱动 IMU 读取→step→publish）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            落地 EKF_CV 融合模式：放行 validate、补 step 分支
 * 2026-06-23       0.3            zeh            补 exec_ops 封装；补全公共函数 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/estimation_fusion.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <math.h>
#include <string.h>

/**
 * @brief 由加速度计分量计算 pitch 角（弧度）
 *
 * 使用 atan2 对 ax、az 进行二象限反正切，返回值范围 (-π/2, π/2)。
 * 当合矢量幅值极小时直接返回 0，避免除零。
 *
 * @param ax X 轴加速度（任意量纲，方向一致即可）
 * @param az Z 轴加速度（同量纲）
 * @return 估算 pitch 角（rad）
 */
static float ekf_cv_accel_pitch(float ax, float az) {
    /* 合矢量近零时（自由落体或无重力分量）直接置 0 */
    if (ax * ax + az * az < 1e-6f) {
        return 0.0f;
    }
    return atan2f(-ax, az);
}

/**
 * @brief 校验融合组件配置
 *
 * 对 EKF_CV 模式额外校验过程噪声与测量噪声须为非负有限值。
 *
 * @param config 指向配置结构体的只读指针，不得为 NULL
 * @return BM_OK 校验通过；BM_ERR_INVALID 参数非法
 */
int bm_estimation_fusion_validate_config(
    const bm_estimation_fusion_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->mode == BM_EST_FUSION_EKF_CV) {
        /* 校验 EKF_CV 所需噪声参数：须为非负有限值 */
        if (config->ekf_cv.q_pos < 0.0f || config->ekf_cv.q_vel < 0.0f ||
            config->ekf_cv.r_pos < 0.0f) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

void bm_estimation_fusion_reset(bm_estimation_fusion_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_complementary_reset(&axis->state.complementary);
    bm_algo_mahony_reset(&axis->state.mahony);
    bm_algo_ekf_cv_reset(&axis->state.ekf_cv, 0.0f, 0.0f);
    memset(&axis->state.euler, 0, sizeof(axis->state.euler));
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_estimation_fusion_init(bm_estimation_fusion_axis_t *axis) {
    if (axis == NULL ||
        bm_estimation_fusion_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_estimation_fusion_reset(axis);
    return BM_OK;
}

void bm_estimation_fusion_step(bm_estimation_fusion_axis_t *axis) {
    const bm_estimation_fusion_config_t *cfg;
    bm_estimation_fusion_state_t *st;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    float ax = 0.0f, ay = 0.0f, az = 1.0f;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_imu == NULL) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_EST_FUSION_TEL_NO_IMU;
        st->telemetry.roll_rad = st->euler.roll_rad;
        st->telemetry.pitch_rad = st->euler.pitch_rad;
        st->telemetry.yaw_rad = st->euler.yaw_rad;
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
        return;
    }

    if (axis->resources.read_imu(axis->resources.read_imu_user,
                                 &gx, &gy, &gz, &ax, &ay, &az) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_EST_FUSION_TEL_STALE;
        st->telemetry.roll_rad = st->euler.roll_rad;
        st->telemetry.pitch_rad = st->euler.pitch_rad;
        st->telemetry.yaw_rad = st->euler.yaw_rad;
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
        return;
    }

    switch (cfg->mode) {
    case BM_EST_FUSION_COMPLEMENTARY:
        bm_algo_complementary_step(&st->complementary, &cfg->complementary,
                                   gx, gy, gz, ax, ay, az, cfg->dt_s);
        st->euler.roll_rad = st->complementary.roll_rad;
        st->euler.pitch_rad = st->complementary.pitch_rad;
        st->euler.yaw_rad = 0.0f;
        break;
    case BM_EST_FUSION_MAHONY:
        bm_algo_mahony_step(&st->mahony, &cfg->mahony,
                            gx, gy, gz, ax, ay, az, cfg->dt_s);
        bm_algo_quat_to_euler(&st->mahony.q, &st->euler);
        break;
    case BM_EST_FUSION_EKF_CV:
        /*
         * EKF-CV（匀速模型）单轴 pitch 跟踪：
         *   - 状态向量：[pitch_rad, d(pitch)/dt]
         *   - 预测：以陀螺仪 gy（绕 Y 轴角速率）更新 ekf_cv.vel，
         *           随后调用 predict 按匀速模型外推 dt_s；
         *   - 更新：以加速度计导出的 pitch 角（atan2(-ax, az)）作位置测量。
         *   - 输出：ekf_cv.pos → pitch_rad；roll/yaw 置 0。
         */
        st->ekf_cv.vel = gy;
        bm_algo_ekf_cv_predict(&st->ekf_cv, &cfg->ekf_cv, cfg->dt_s);
        bm_algo_ekf_cv_update(&st->ekf_cv, &cfg->ekf_cv,
                              ekf_cv_accel_pitch(ax, az));
        st->euler.roll_rad  = 0.0f;
        st->euler.pitch_rad = st->ekf_cv.pos;
        st->euler.yaw_rad   = 0.0f;
        break;
    default:
        break;
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_EST_FUSION_TEL_VALID;
    st->telemetry.roll_rad = st->euler.roll_rad;
    st->telemetry.pitch_rad = st->euler.pitch_rad;
    st->telemetry.yaw_rad = st->euler.yaw_rad;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/* ---------- exec_ops 封装 ---------- */

void bm_estimation_fusion_exec_run(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_estimation_fusion_step(
            (bm_estimation_fusion_axis_t *)instance->state);
    }
}

int bm_estimation_fusion_exec_init(const bm_exec_t *instance) {
    bm_estimation_fusion_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_estimation_fusion_axis_t *)instance->state;
    return bm_estimation_fusion_init(axis);
}

int bm_estimation_fusion_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_estimation_fusion_exec_safe_stop(const bm_exec_t *instance) {
    bm_estimation_fusion_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_estimation_fusion_axis_t *)instance->state;
    /* 安全停止：清零欧拉角输出，避免下游读到陈旧姿态 */
    axis->state.euler.roll_rad  = 0.0f;
    axis->state.euler.pitch_rad = 0.0f;
    axis->state.euler.yaw_rad   = 0.0f;
}

const bm_exec_ops_t bm_estimation_fusion_exec_ops = {
    bm_estimation_fusion_exec_init,
    bm_estimation_fusion_exec_start,
    bm_estimation_fusion_exec_safe_stop
};

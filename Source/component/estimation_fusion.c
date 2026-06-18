/**
 * @file estimation_fusion.c
 * @brief 估算融合选择器组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 *
 */
#include "bm/component/estimation_fusion.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_estimation_fusion_validate_config(
    const bm_estimation_fusion_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->mode == BM_EST_FUSION_EKF_CV) {
        return BM_ERR_INVALID;
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
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
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
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
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
    default:
        break;
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_EST_FUSION_TEL_VALID;
    st->telemetry.roll_rad = st->euler.roll_rad;
    st->telemetry.pitch_rad = st->euler.pitch_rad;
    st->telemetry.yaw_rad = st->euler.yaw_rad;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

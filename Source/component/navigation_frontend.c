/**
 * @file navigation_frontend.c
 * @brief GNSS/IMU/轮速对齐与门控融合实现
 *
 * 按时间戳选取最新样本，经 EKF 门控更新线速度状态并发布遥测。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            validate_config 噪声/权重校验；SPDX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/navigation_frontend.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <math.h>
#include <string.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

static int ts_aligned(uint32_t a, uint32_t b, uint32_t tol_us) {
    uint32_t d;
    uint32_t diff;

    if (a == 0u || b == 0u) {
        return 0;
    }
    /*
     * u32 微秒时间戳约 71.6 分钟回绕一次（P1-15）。朴素的 (a>b)?(a-b):(b-a)
     * 在回绕瞬间会把邻近两拍误判为相隔数千万微秒。改用回绕安全差：无符号减法
     * 得模 2^32 差，超过半程则取补码绝对值，得到真正的最短时间间隔。
     */
    d = a - b;
    diff = (d > 0x80000000u) ? (uint32_t)(-(int32_t)d) : d;
    return (diff <= tol_us) ? 1 : 0;
}

static float rpm_to_m_s(float rpm, float radius_m) {
    return rpm * (2.0f * BM_ALGO_PI_F / 60.0f) * radius_m;
}

int bm_navigation_frontend_validate_config(
    const bm_navigation_frontend_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->wheel_radius_m <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* 融合权重须非负 */
    if (config->gnss_weight < 0.0f || config->wheel_weight < 0.0f) {
        return BM_ERR_INVALID;
    }
    /* EKF 噪声参数须正值 */
    if (config->ekf.q_pos <= 0.0f || config->ekf.q_vel <= 0.0f ||
        config->ekf.r_pos <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_navigation_frontend_reset(bm_navigation_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_ekf_cv_reset(&axis->state.ekf, 0.0f, 0.0f);
    axis->state.gnss_vel = 0.0f;
    axis->state.wheel_vel = 0.0f;
    axis->state.gyro_rad_s = 0.0f;
    axis->state.gnss_ts_us = 0u;
    axis->state.wheel_ts_us = 0u;
    axis->state.imu_ts_us = 0u;
    axis->state.have_gnss = 0;
    axis->state.have_wheel = 0;
    axis->state.fused_velocity_m_s = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_navigation_frontend_init(bm_navigation_frontend_axis_t *axis) {
    if (axis == NULL ||
        bm_navigation_frontend_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_navigation_frontend_reset(axis);
    return BM_OK;
}

void bm_navigation_frontend_step(bm_navigation_frontend_axis_t *axis) {
    const bm_navigation_frontend_config_t *cfg;
    bm_navigation_frontend_state_t *st;
    float gnss_v = 0.0f;
    float gyro = 0.0f;
    float rpm = 0.0f;
    uint32_t gnss_ts = 0u;
    uint32_t imu_ts = 0u;
    uint32_t wheel_ts = 0u;
    uint32_t status;
    float wheel_v;
    int gnss_ok = 0;
    int wheel_ok = 0;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_gnss != NULL &&
        axis->resources.read_gnss(axis->resources.read_gnss_user,
                                  &gnss_v, &gnss_ts) == 0) {
        st->gnss_vel = gnss_v;
        st->gnss_ts_us = gnss_ts;
        st->have_gnss = 1;
        gnss_ok = 1;
    }

    if (axis->resources.read_wheel != NULL &&
        axis->resources.read_wheel(axis->resources.read_wheel_user,
                                   &rpm, &wheel_ts) == 0) {
        wheel_v = rpm_to_m_s(rpm, cfg->wheel_radius_m);
        st->wheel_vel = wheel_v;
        st->wheel_ts_us = wheel_ts;
        st->have_wheel = 1;
        wheel_ok = 1;
    }

    if (axis->resources.read_imu != NULL &&
        axis->resources.read_imu(axis->resources.read_imu_user,
                                 &gyro, &imu_ts) == 0) {
        st->gyro_rad_s = gyro;
        st->imu_ts_us = imu_ts;
    }

    bm_algo_ekf_cv_predict(&st->ekf, &cfg->ekf, cfg->dt_s);

    status = BM_NAV_FRONTEND_TEL_VALID;

    if (wheel_ok && gnss_ok &&
        ts_aligned(gnss_ts, wheel_ts, cfg->align_tolerance_us)) {
        float blended = cfg->gnss_weight * gnss_v + cfg->wheel_weight * wheel_v;

        if (bm_algo_ekf_cv_update_gated(&st->ekf, &cfg->ekf, blended,
                                        &cfg->gate) == BM_ALGO_EKF_UPDATE_OK) {
            st->fused_velocity_m_s = st->ekf.pos;
        } else {
            st->fused_velocity_m_s = wheel_v;
            status |= BM_NAV_FRONTEND_TEL_GNSS_STALE;
        }
    } else if (wheel_ok) {
        st->fused_velocity_m_s = wheel_v;
        status |= BM_NAV_FRONTEND_TEL_WHEEL_ONLY;
    } else if (gnss_ok) {
        st->fused_velocity_m_s = gnss_v;
    } else {
        st->fused_velocity_m_s = st->ekf.pos;
        status |= BM_NAV_FRONTEND_TEL_GNSS_STALE;
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = status;
    st->telemetry.velocity_m_s = st->fused_velocity_m_s;
    st->telemetry.gnss_velocity_m_s = st->gnss_vel;
    st->telemetry.wheel_velocity_m_s = st->wheel_vel;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/**
 * @file mobile_base_control.c
 * @brief 差速底盘运动学实现
 *
 * v, ω 经轮距换算左右轮线速度，可选坡道前馈叠加。
 * 通过 bm_mobile_base_control_exec_ops 接入 bm_exec 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 exec_ops、Doxygen、SPDX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/mobile_base_control.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

/**
 * @brief 将轮速限幅至 [-max_v, +max_v]
 *
 * @param v     待限幅轮速，单位 m/s
 * @param max_v 最大绝对值，必须 > 0
 * @return 限幅后的轮速
 */
static float clamp_wheel(float v, float max_v) {
    return bm_algo_clamp_f(v, -max_v, max_v);
}

int bm_mobile_base_control_validate_config(
    const bm_mobile_base_control_config_t *config) {
    if (config == NULL || config->wheel_base_m <= 0.0f ||
        config->wheel_radius_m <= 0.0f ||
        config->max_wheel_m_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_mobile_base_control_reset(bm_mobile_base_control_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    axis->state.linear_cmd_m_s = 0.0f;
    axis->state.angular_cmd_rad_s = 0.0f;
    axis->state.left_m_s = 0.0f;
    axis->state.right_m_s = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_mobile_base_control_init(bm_mobile_base_control_axis_t *axis) {
    if (axis == NULL ||
        bm_mobile_base_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_mobile_base_control_reset(axis);
    return BM_OK;
}

void bm_mobile_base_control_set_cmd(bm_mobile_base_control_axis_t *axis,
                                    float linear_m_s,
                                    float angular_rad_s) {
    if (axis == NULL) {
        return;
    }
    axis->state.linear_cmd_m_s = linear_m_s;
    axis->state.angular_cmd_rad_s = angular_rad_s;
}

void bm_mobile_base_control_step(bm_mobile_base_control_axis_t *axis) {
    const bm_mobile_base_control_config_t *cfg;
    bm_mobile_base_control_state_t *st;
    float half_base;
    float v;
    float w;
    float left;
    float right;
    float slope_ff = 0.0f;

    if (axis == NULL ||
        bm_mobile_base_control_validate_config(&axis->config) != BM_OK) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;
    half_base = cfg->wheel_base_m * 0.5f;
    v = st->linear_cmd_m_s;
    w = st->angular_cmd_rad_s;

    if (cfg->enable_slope_feedforward) {
        slope_ff = cfg->slope_feedforward_gain *
                   sinf(cfg->slope_angle_rad) * 9.81f;
        v += slope_ff;
    }

    left = v - w * half_base;
    right = v + w * half_base;

    left = clamp_wheel(left, cfg->max_wheel_m_s);
    right = clamp_wheel(right, cfg->max_wheel_m_s);

    st->left_m_s = left;
    st->right_m_s = right;

    if (axis->resources.write_wheels != NULL) {
        (void)axis->resources.write_wheels(axis->resources.write_wheels_user,
                                           left, right);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.linear_m_s = st->linear_cmd_m_s;
    st->telemetry.angular_rad_s = st->angular_cmd_rad_s;
    st->telemetry.left_wheel_m_s = left;
    st->telemetry.right_wheel_m_s = right;
    st->telemetry.slope_feedforward_m_s = slope_ff;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

void bm_mobile_base_control_exec_step(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_mobile_base_control_step(
            (bm_mobile_base_control_axis_t *)instance->state);
    }
}

int bm_mobile_base_control_exec_init(const bm_exec_t *instance) {
    bm_mobile_base_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_mobile_base_control_axis_t *)instance->state;
    if (bm_mobile_base_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_mobile_base_control_reset(axis);
    return BM_OK;
}

int bm_mobile_base_control_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_mobile_base_control_exec_safe_stop(const bm_exec_t *instance) {
    bm_mobile_base_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_mobile_base_control_axis_t *)instance->state;
    axis->state.linear_cmd_m_s = 0.0f;
    axis->state.angular_cmd_rad_s = 0.0f;
    axis->state.left_m_s = 0.0f;
    axis->state.right_m_s = 0.0f;
    if (axis->resources.write_wheels != NULL) {
        (void)axis->resources.write_wheels(
            axis->resources.write_wheels_user, 0.0f, 0.0f);
    }
}

const bm_exec_ops_t bm_mobile_base_control_exec_ops = {
    bm_mobile_base_control_exec_init,
    bm_mobile_base_control_exec_start,
    bm_mobile_base_control_exec_safe_stop
};

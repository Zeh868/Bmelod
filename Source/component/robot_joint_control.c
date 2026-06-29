/**
 * @file robot_joint_control.c
 * @brief 单关节力矩 PI 控制实现
 *
 * 位置误差经 PI 输出力矩，叠加摩擦前馈并限幅。
 * 通过 bm_robot_joint_control_exec_ops 接入 bm_exec 生命周期。
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
#include "bm/component/robot_joint_control.h"
#include "bm/algorithm/bm_algo_compensation.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

int bm_robot_joint_control_validate_config(
    const bm_robot_joint_control_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->torque_max_nm <= 0.0f ||
        config->velocity_max_rad_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_pi_validate_config(&config->pi) != 0) {
        return BM_ERR_INVALID;
    }
    if (config->position_min_rad > config->position_max_rad) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_robot_joint_control_reset(bm_robot_joint_control_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_pi_reset(&axis->state.pi, 0.0f);
    axis->state.torque_cmd_nm = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_robot_joint_control_init(bm_robot_joint_control_axis_t *axis) {
    if (axis == NULL ||
        bm_robot_joint_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_robot_joint_control_reset(axis);
    return BM_OK;
}

void bm_robot_joint_control_step(bm_robot_joint_control_axis_t *axis) {
    const bm_robot_joint_control_config_t *cfg;
    bm_robot_joint_control_state_t *st;
    float pos = 0.0f;
    float vel = 0.0f;
    float setpoint;
    float err;
    float pi_out;
    float friction_ff;
    float torque;

    if (axis == NULL ||
        bm_robot_joint_control_validate_config(&axis->config) != BM_OK) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_joint != NULL &&
        axis->resources.read_joint(axis->resources.read_joint_user,
                                   &pos, &vel) != 0) {
        st->step_count++;
        return;
    }

    if (fabsf(vel) > cfg->velocity_max_rad_s) {
        vel = (vel > 0.0f) ? cfg->velocity_max_rad_s : -cfg->velocity_max_rad_s;
    }

    setpoint = bm_algo_clamp_f(cfg->position_setpoint_rad,
                               cfg->position_min_rad,
                               cfg->position_max_rad);
    err = setpoint - pos;
    pi_out = bm_algo_pi_step(&st->pi, &cfg->pi, err, cfg->dt_s);

    friction_ff = bm_algo_friction_comp(vel, cfg->coulomb_friction,
                                      cfg->viscous_friction,
                                      cfg->friction_deadband);
    torque = bm_algo_clamp_f(pi_out + friction_ff,
                             -cfg->torque_max_nm, cfg->torque_max_nm);

    st->torque_cmd_nm = torque;

    if (axis->resources.write_torque != NULL) {
        (void)axis->resources.write_torque(axis->resources.write_torque_user,
                                           torque);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.position_rad = pos;
    st->telemetry.velocity_rad_s = vel;
    st->telemetry.torque_nm = torque;
    st->telemetry.friction_ff_nm = friction_ff;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

void bm_robot_joint_control_exec_step(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_robot_joint_control_step(
            (bm_robot_joint_control_axis_t *)instance->state);
    }
}

int bm_robot_joint_control_exec_init(const bm_exec_t *instance) {
    bm_robot_joint_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_robot_joint_control_axis_t *)instance->state;
    if (bm_robot_joint_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_robot_joint_control_reset(axis);
    return BM_OK;
}

int bm_robot_joint_control_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_robot_joint_control_exec_safe_stop(const bm_exec_t *instance) {
    bm_robot_joint_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_robot_joint_control_axis_t *)instance->state;
    axis->state.torque_cmd_nm = 0.0f;
    bm_algo_pi_reset(&axis->state.pi, 0.0f);
    if (axis->resources.write_torque != NULL) {
        (void)axis->resources.write_torque(
            axis->resources.write_torque_user, 0.0f);
    }
}

const bm_exec_ops_t bm_robot_joint_control_exec_ops = {
    bm_robot_joint_control_exec_init,
    bm_robot_joint_control_exec_start,
    bm_robot_joint_control_exec_safe_stop
};

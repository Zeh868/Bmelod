/**
 * @file power_control.c
 * @brief Buck 双环电源控制组件实现
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
#include "bm/component/power_control.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <string.h>

static void latch_fault(bm_power_control_axis_t *axis) {
    bm_power_control_state_t *st = &axis->state;

    if (!st->fault_latched) {
        st->fault_latched = 1;
    }
    st->i_ref_a = 0.0f;
    st->duty = axis->config.duty_min;
    bm_algo_pi_reset(&st->pi_voltage, 0.0f);
    bm_algo_pi_reset(&st->pi_current, 0.0f);
    if (axis->resources.write_duty != NULL) {
        (void)axis->resources.write_duty(axis->resources.write_duty_user,
                                         st->duty);
    }
}

static void sync_command(bm_power_control_axis_t *axis) {
    bm_power_ctrl_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_power_control_apply_command(axis, &command);
    }
}

int bm_power_control_validate_config(const bm_power_control_config_t *config) {
    if (config == NULL || config->voltage_dt_s <= 0.0f ||
        config->current_dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->duty_max <= config->duty_min) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_power_control_reset(bm_power_control_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_pi_reset(&axis->state.pi_voltage, 0.0f);
    bm_algo_pi_reset(&axis->state.pi_current, 0.0f);
    bm_algo_ramp_reset(&axis->state.v_ramp, 0.0f);
    axis->state.i_ref_a = 0.0f;
    axis->state.duty = axis->config.duty_min;
    axis->state.v_target_v = 0.0f;
    axis->state.fault_latched = 0;
    axis->state.voltage_loops = 0u;
    axis->state.current_loops = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

void bm_power_control_apply_command(bm_power_control_axis_t *axis,
                                    const bm_power_ctrl_cmd_t *cmd) {
    if (axis == NULL || cmd == NULL) {
        return;
    }

    axis->state.cmd = *cmd;
    if ((cmd->status & BM_POWER_CTRL_CMD_FAULT) != 0u) {
        latch_fault(axis);
    }
}

void bm_power_control_voltage_step(bm_power_control_axis_t *axis) {
    const bm_power_control_config_t *cfg;
    bm_power_control_state_t *st;
    float v_out = 0.0f;
    float i_out = 0.0f;
    float v_ramped;
    float i_cmd;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    sync_command(axis);

    if (st->fault_latched ||
        (st->cmd.status & BM_POWER_CTRL_CMD_ENABLED) == 0u) {
        st->i_ref_a = 0.0f;
        return;
    }

    if (axis->resources.read_feedback != NULL &&
        axis->resources.read_feedback(axis->resources.read_feedback_user,
                                      &v_out, &i_out) != 0) {
        latch_fault(axis);
        return;
    }

    v_ramped = bm_algo_ramp_step(&st->v_ramp, &cfg->v_ramp,
                                 st->cmd.v_set_v, cfg->voltage_dt_s);
    st->v_target_v = v_ramped;

    i_cmd = bm_algo_pi_step(&st->pi_voltage, &cfg->pi_voltage,
                            v_ramped - v_out, cfg->voltage_dt_s);
    st->i_ref_a = bm_algo_clamp_f(i_cmd, -cfg->i_limit_a, cfg->i_limit_a);

    st->voltage_loops++;
    st->telemetry.v_out_v = v_out;
    st->telemetry.i_out_a = i_out;
    st->telemetry.v_set_v = v_ramped;
}

void bm_power_control_current_step(bm_power_control_axis_t *axis) {
    const bm_power_control_config_t *cfg;
    bm_power_control_state_t *st;
    float v_out = 0.0f;
    float i_out = 0.0f;
    float duty_cmd;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (st->fault_latched) {
        st->telemetry.status = BM_POWER_CTRL_TEL_FAULT;
        st->telemetry.duty = st->duty;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    if (axis->resources.read_feedback != NULL) {
        (void)axis->resources.read_feedback(
            axis->resources.read_feedback_user, &v_out, &i_out);
    }

    duty_cmd = bm_algo_pi_step(&st->pi_current, &cfg->pi_current,
                               st->i_ref_a - i_out, cfg->current_dt_s);
    st->duty = bm_algo_clamp_f(duty_cmd, cfg->duty_min, cfg->duty_max);

    if (axis->resources.write_duty != NULL) {
        if (axis->resources.write_duty(axis->resources.write_duty_user,
                                       st->duty) != 0) {
            latch_fault(axis);
            return;
        }
    }

    st->current_loops++;
    st->telemetry.sequence = st->current_loops;
    st->telemetry.status = BM_POWER_CTRL_TEL_VALID;
    st->telemetry.duty = st->duty;
    st->telemetry.i_out_a = i_out;
    st->telemetry.v_out_v = v_out;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

void bm_power_control_exec_voltage(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_power_control_voltage_step((bm_power_control_axis_t *)instance->state);
    }
}

void bm_power_control_exec_current(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_power_control_current_step((bm_power_control_axis_t *)instance->state);
    }
}

int bm_power_control_exec_init(const bm_exec_t *instance) {
    bm_power_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_power_control_axis_t *)instance->state;
    if (bm_power_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_power_control_reset(axis);
    return BM_OK;
}

int bm_power_control_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_power_control_exec_safe_stop(const bm_exec_t *instance) {
    bm_power_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_power_control_axis_t *)instance->state;
    axis->state.i_ref_a = 0.0f;
    axis->state.duty = axis->config.duty_min;
    if (axis->resources.write_duty != NULL) {
        (void)axis->resources.write_duty(axis->resources.write_duty_user,
                                         axis->state.duty);
    }
}

const bm_exec_ops_t bm_power_control_exec_ops = {
    bm_power_control_exec_init,
    bm_power_control_exec_start,
    bm_power_control_exec_safe_stop
};

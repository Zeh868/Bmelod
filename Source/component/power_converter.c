/**
 * @file power_converter.c
 * @brief Buck 峰值电流模式组件实现
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
#include "bm/component/power_converter.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <string.h>

static void publish_pwr_conv_telemetry(bm_power_converter_axis_t *axis) {
    if (axis == NULL || axis->resources.publish_telemetry == NULL) {
        return;
    }
    axis->resources.publish_telemetry(
        axis->resources.publish_telemetry_user, &axis->state.telemetry);
}

static void latch_fault(bm_power_converter_axis_t *axis) {
    bm_power_converter_state_t *st = &axis->state;

    if (!st->fault_latched) {
        st->fault_latched = 1;
    }
    st->i_ref_a = 0.0f;
    st->duty = axis->config.duty_min;
    bm_algo_pi_reset(&st->pi_current, 0.0f);
    if (axis->resources.write_duty != NULL) {
        (void)axis->resources.write_duty(axis->resources.write_duty_user,
                                         st->duty);
    }
}

static void sync_command(bm_power_converter_axis_t *axis) {
    bm_pwr_conv_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_power_converter_apply_command(axis, &command);
    }
}

int bm_power_converter_validate_config(const bm_power_converter_config_t *config) {
    if (config == NULL || config->current_dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->duty_max <= config->duty_min) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_power_converter_reset(bm_power_converter_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_pi_reset(&axis->state.pi_current, 0.0f);
    bm_algo_ramp_reset(&axis->state.i_ramp, 0.0f);
    axis->state.i_ref_a = 0.0f;
    axis->state.duty = axis->config.duty_min;
    axis->state.fault_latched = 0;
    axis->state.current_loops = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

void bm_power_converter_clear_fault(bm_power_converter_axis_t *axis) {
    if (axis == NULL || !axis->state.fault_latched) {
        return;
    }
    axis->state.fault_latched = 0;
    bm_algo_pi_reset(&axis->state.pi_current, 0.0f);
    axis->state.telemetry.status &= (uint32_t)~BM_PWR_CONV_TEL_FAULT;
}

void bm_power_converter_apply_command(bm_power_converter_axis_t *axis,
                                      const bm_pwr_conv_cmd_t *cmd) {
    if (axis == NULL || cmd == NULL) {
        return;
    }

    axis->state.cmd = *cmd;
    if ((cmd->status & BM_PWR_CONV_CMD_FAULT) != 0u) {
        latch_fault(axis);
    }
}

void bm_power_converter_current_step(bm_power_converter_axis_t *axis) {
    const bm_power_converter_config_t *cfg;
    bm_power_converter_state_t *st;
    float i_out = 0.0f;
    float i_ramped;
    float duty_cmd;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    sync_command(axis);

    if (st->fault_latched) {
        st->telemetry.status = BM_PWR_CONV_TEL_FAULT;
        st->telemetry.duty = st->duty;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    if ((st->cmd.status & BM_PWR_CONV_CMD_ENABLED) == 0u) {
        st->i_ref_a = 0.0f;
        bm_algo_ramp_reset(&st->i_ramp, 0.0f);
        st->duty = cfg->duty_min;
        if (axis->resources.write_duty != NULL) {
            (void)axis->resources.write_duty(axis->resources.write_duty_user,
                                             st->duty);
        }
        return;
    }

    if (axis->resources.read_current != NULL &&
        axis->resources.read_current(axis->resources.read_current_user,
                                     &i_out) != 0) {
        latch_fault(axis);
        st->telemetry.sequence = st->current_loops;
        st->telemetry.status = BM_PWR_CONV_TEL_FAULT;
        st->telemetry.i_set_a = st->cmd.i_set_a;
        st->telemetry.i_ref_a = st->i_ref_a;
        st->telemetry.i_out_a = 0.0f;
        st->telemetry.duty = st->duty;
        publish_pwr_conv_telemetry(axis);
        return;
    }

    i_ramped = bm_algo_ramp_step(&st->i_ramp, &cfg->i_ramp,
                                 st->cmd.i_set_a, cfg->current_dt_s);
    st->i_ref_a = i_ramped;

    duty_cmd = bm_algo_pi_step(&st->pi_current, &cfg->pi_current,
                               st->i_ref_a - i_out, cfg->current_dt_s);
    st->duty = bm_algo_clamp_f(duty_cmd, cfg->duty_min, cfg->duty_max);

    if (axis->resources.write_duty != NULL) {
        if (axis->resources.write_duty(axis->resources.write_duty_user,
                                       st->duty) != 0) {
            latch_fault(axis);
            st->telemetry.sequence = st->current_loops;
            st->telemetry.status = BM_PWR_CONV_TEL_FAULT;
            st->telemetry.i_set_a = st->cmd.i_set_a;
            st->telemetry.i_ref_a = st->i_ref_a;
            st->telemetry.i_out_a = i_out;
            st->telemetry.duty = st->duty;
            publish_pwr_conv_telemetry(axis);
            return;
        }
    }

    st->current_loops++;
    st->telemetry.sequence = st->current_loops;
    st->telemetry.status = BM_PWR_CONV_TEL_VALID;
    st->telemetry.i_set_a = st->cmd.i_set_a;
    st->telemetry.i_ref_a = st->i_ref_a;
    st->telemetry.i_out_a = i_out;
    st->telemetry.duty = st->duty;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

void bm_power_converter_exec_current(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_power_converter_current_step(
            (bm_power_converter_axis_t *)instance->state);
    }
}

int bm_power_converter_exec_init(const bm_exec_t *instance) {
    bm_power_converter_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_power_converter_axis_t *)instance->state;
    if (bm_power_converter_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_power_converter_reset(axis);
    return BM_OK;
}

int bm_power_converter_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_power_converter_exec_safe_stop(const bm_exec_t *instance) {
    bm_power_converter_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_power_converter_axis_t *)instance->state;
    axis->state.i_ref_a = 0.0f;
    axis->state.duty = axis->config.duty_min;
    if (axis->resources.write_duty != NULL) {
        (void)axis->resources.write_duty(axis->resources.write_duty_user,
                                         axis->state.duty);
    }
}

const bm_exec_ops_t bm_power_converter_exec_ops = {
    bm_power_converter_exec_init,
    bm_power_converter_exec_start,
    bm_power_converter_exec_safe_stop
};

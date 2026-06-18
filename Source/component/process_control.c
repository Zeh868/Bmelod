/**
 * @file process_control.c
 * @brief Smith 预估 + PID 串级过程控制实现
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
#include "bm/component/process_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_process_control_validate_config(const bm_process_control_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->smith_delay_line == NULL || config->smith_line_len == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_process_control_reset(bm_process_control_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_pid_reset(&axis->state.outer_pid, 0.0f);
    bm_algo_pid_reset(&axis->state.inner_pid, 0.0f);
    bm_algo_smith_predictor_reset(&axis->state.smith, &axis->config.smith);
    axis->state.outer_out = 0.0f;
    axis->state.inner_out = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_process_control_init(bm_process_control_axis_t *axis) {
    if (axis == NULL ||
        bm_process_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_smith_predictor_init(&axis->state.smith, &axis->config.smith,
                                     axis->config.smith_delay_line,
                                     axis->config.smith_line_len) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_process_control_reset(axis);
    return BM_OK;
}

void bm_process_control_step(bm_process_control_axis_t *axis) {
    const bm_process_control_config_t *cfg;
    bm_process_control_state_t *st;
    float setpoint = 0.0f;
    float measurement = 0.0f;
    float smith_error;
    float inner_meas;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_io != NULL &&
        axis->resources.read_io(axis->resources.read_io_user,
                                &setpoint, &measurement) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_PROCESS_CTRL_TEL_STALE;
        st->telemetry.outer_out = st->outer_out;
        st->telemetry.inner_out = st->inner_out;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    st->outer_out = bm_algo_pid_step(&st->outer_pid, &cfg->outer_pid,
                                     setpoint - measurement, cfg->dt_s);
    inner_meas = measurement;
    smith_error = bm_algo_smith_predictor_step(&st->smith, &cfg->smith,
                                               st->outer_out, inner_meas,
                                               st->outer_out);
    st->inner_out = bm_algo_pid_step(&st->inner_pid, &cfg->inner_pid,
                                     smith_error, cfg->dt_s);

    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           st->inner_out);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_PROCESS_CTRL_TEL_VALID;
    st->telemetry.setpoint = setpoint;
    st->telemetry.measurement = measurement;
    st->telemetry.outer_out = st->outer_out;
    st->telemetry.inner_out = st->inner_out;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

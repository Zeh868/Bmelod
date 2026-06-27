/**
 * @file process_control.c
 * @brief Smith 预估 + PID 串级过程控制实现
 *
 * 封装 Smith 预估器与外环/内环 PID 串级控制，
 * 并提供 bm_exec_ops_t 调度封装。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 validate_config Smith 参数校验；补 exec_ops 封装
 * 2026-06-23       0.3            zeh            修复 Smith 预估器 u_controller 误传 outer_out（应为 inner_out）
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/process_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_process_control_validate_config(const bm_process_control_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* 延迟线缓冲区须有效 */
    if (config->smith_delay_line == NULL || config->smith_line_len == 0u) {
        return BM_ERR_INVALID;
    }
    /* Smith 模型增益须为正值（增益为零物理上无意义） */
    if (config->smith.model_gain <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* 延迟步数须在缓冲区范围内（delay_steps=0 表示无延迟，合法） */
    if (config->smith.delay_steps >= config->smith_line_len) {
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
    /* Smith 预估器的 u_controller 应为上一拍实际施加的内环控制量
     * （st->inner_out，本拍尚未更新即上一拍值），而非外环参考 outer_out；
     * 误传 outer_out 会使模型补偿项恒抵消、内环输入恒零、输出恒零。 */
    smith_error = bm_algo_smith_predictor_step(&st->smith, &cfg->smith,
                                               st->outer_out, inner_meas,
                                               st->inner_out);
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

/* ---------- exec_ops 封装 ---------- */

void bm_process_control_exec_run(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_process_control_step((bm_process_control_axis_t *)instance->state);
    }
}

int bm_process_control_exec_init(const bm_exec_t *instance) {
    bm_process_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_process_control_axis_t *)instance->state;
    return bm_process_control_init(axis);
}

int bm_process_control_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_process_control_exec_safe_stop(const bm_exec_t *instance) {
    bm_process_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_process_control_axis_t *)instance->state;
    /* 安全停止：清零输出并写入硬件 */
    axis->state.outer_out = 0.0f;
    axis->state.inner_out = 0.0f;
    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           0.0f);
    }
}

const bm_exec_ops_t bm_process_control_exec_ops = {
    bm_process_control_exec_init,
    bm_process_control_exec_start,
    bm_process_control_exec_safe_stop
};

/**
 * @file power_control.c
 * @brief Buck 双环电源控制组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/power_control.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

/**
 * @brief 锁存故障并将占空比降至 duty_min（静态辅助）
 *
 * 置 fault_latched，清零 i_ref_a，将占空比设为 duty_min，
 * 复位两级 PI 积分器，并通过 write_duty 回调写入安全占空比。
 *
 * @param axis 实例指针
 */
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

/**
 * @brief 从回调读取最新命令并应用（静态辅助）
 *
 * read_command 回调返回 0 时调用 bm_power_control_apply_command()；
 * 回调为 NULL 或返回非零时保持旧命令。
 *
 * @param axis 实例指针
 */
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
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
        return;
    }

    /*
     * 未使能时输出安全占空比并冻结电流环积分（P2-4）。此前 current_step 不检查
     * ENABLED，禁用状态下仍以 i_ref=0 运行 PI 并写 duty，与 power_converter
     * “disable 即输出安全 duty” 语义不一致，且积分器持续累积。此处对齐语义：
     * 降至 duty_min、复位积分器、写安全值后照常发布遥测。
     */
    if ((st->cmd.status & BM_POWER_CTRL_CMD_ENABLED) == 0u) {
        st->duty = cfg->duty_min;
        bm_algo_pi_reset(&st->pi_current, 0.0f);
        if (axis->resources.write_duty != NULL) {
            (void)axis->resources.write_duty(axis->resources.write_duty_user,
                                             st->duty);
        }
        st->current_loops++;
        st->telemetry.sequence = st->current_loops;
        st->telemetry.status = BM_POWER_CTRL_TEL_VALID;
        st->telemetry.duty = st->duty;
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
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

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/**
 * @brief exec 封装：执行一步电压环（供调度框架慢时基槽调用）
 *
 * @param instance exec 实例指针，instance->state 须为 bm_power_control_axis_t*
 */
void bm_power_control_exec_voltage(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_power_control_voltage_step((bm_power_control_axis_t *)instance->state);
    }
}

/**
 * @brief exec 封装：执行一步电流环（供调度框架快时基槽调用）
 *
 * @param instance exec 实例指针，instance->state 须为 bm_power_control_axis_t*
 */
void bm_power_control_exec_current(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_power_control_current_step((bm_power_control_axis_t *)instance->state);
    }
}

/**
 * @brief exec 生命周期：初始化（校验配置并复位状态）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
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

/**
 * @brief exec 生命周期：启动（当前无额外操作，保留扩展点）
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_power_control_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec 生命周期：安全停机（电流参考归零，占空比降至 duty_min）
 *
 * 清零 i_ref_a，将 duty 设为 duty_min，并通过 write_duty 回调
 * 写入安全占空比，确保输出安全关断。
 *
 * @param instance exec 实例指针
 */
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

/**
 * @brief power_control 标准 exec ops 表
 *
 * 将此指针赋给 bm_exec_t::ops，即可将 power_control 实例
 * 接入调度框架的生命周期管理。
 */
const bm_exec_ops_t bm_power_control_exec_ops = {
    bm_power_control_exec_init,
    bm_power_control_exec_start,
    bm_power_control_exec_safe_stop
};

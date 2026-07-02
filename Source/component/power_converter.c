/**
 * @file power_converter.c
 * @brief Buck 峰值电流模式组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补全 Doxygen 中文注释；添加 SPDX 头
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/power_converter.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

/**
 * @brief 通过已配置的回调发布遥测快照
 *
 * 若 axis 为 NULL 或回调指针为 NULL 则静默返回。
 *
 * @param axis 功率变换器轴实例指针
 */
static void publish_pwr_conv_telemetry(bm_power_converter_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &axis->state.telemetry);
}

/**
 * @brief 内部故障锁存：置位 fault_latched，将 i_ref_a 清零，
 *        占空比钳至 duty_min，并通过 write_duty 回调立即输出
 *
 * 调用方在锁存后应自行填写遥测并发布。
 *
 * @param axis 功率变换器轴实例指针（调用方保证非 NULL）
 */
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

/**
 * @brief 通过 read_command 回调同步外部指令到 axis->state.cmd
 *
 * 若回调为 NULL 或回调返回非零则跳过本次同步；
 * 成功读取后调用 @ref bm_power_converter_apply_command 应用。
 *
 * @param axis 功率变换器轴实例指针（调用方保证非 NULL）
 */
static void sync_command(bm_power_converter_axis_t *axis) {
    bm_pwr_conv_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_power_converter_apply_command(axis, &command);
    }
}

/**
 * @brief 校验功率变换器配置合法性
 *
 * @param config 配置结构体指针（const），NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段不合法
 */
int bm_power_converter_validate_config(const bm_power_converter_config_t *config) {
    if (config == NULL || config->current_dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->duty_max <= config->duty_min) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 复位功率变换器轴运行状态
 *
 * 重置 PI 与斜坡状态，清零 i_ref_a、fault_latched、current_loops 及遥测。
 * duty 初始化为 duty_min。NULL 时静默返回。
 *
 * @param axis 功率变换器轴实例指针，NULL 时静默返回
 */
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

/**
 * @brief 清除故障锁存，允许重新运行
 *
 * 重置 PI 积分器并清除遥测中的 FAULT 位。
 * axis 为 NULL 或当前未锁存时静默返回。
 *
 * @param axis 功率变换器轴实例指针，NULL 时静默返回
 */
void bm_power_converter_clear_fault(bm_power_converter_axis_t *axis) {
    if (axis == NULL || !axis->state.fault_latched) {
        return;
    }
    axis->state.fault_latched = 0;
    bm_algo_pi_reset(&axis->state.pi_current, 0.0f);
    axis->state.telemetry.status &= (uint32_t)~BM_PWR_CONV_TEL_FAULT;
}

/**
 * @brief 应用外部指令到轴状态
 *
 * 将 cmd 复制到 state.cmd；若指令中 BM_PWR_CONV_CMD_FAULT 置位则
 * 立即调用内部 latch_fault。
 *
 * @param axis 功率变换器轴实例指针，NULL 时静默返回
 * @param cmd  外部指令结构体指针（const），NULL 时静默返回
 */
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

/**
 * @brief 电流快环单步更新
 *
 * 执行流程：
 * 1. 通过 read_command 回调同步最新指令（可选）。
 * 2. 若 fault_latched：发布 FAULT 遥测并立即返回。
 * 3. 若指令未使能：清零 i_ref_a 与 duty，输出 duty_min。
 * 4. 读取实际电流（失败则锁存故障并返回）。
 * 5. 电流参考经斜坡限速，PI 调节误差 → 占空比，钳位到 [duty_min, duty_max]。
 * 6. 输出占空比（失败则锁存故障）；更新遥测并发布。
 *
 * @param axis 功率变换器轴实例指针，NULL 时静默返回
 */
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
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
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

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/**
 * @brief exec_ops 快环包装：从 bm_exec_t 提取轴指针后调用 current_step
 *
 * @param instance exec 实例指针，NULL 或 state 为 NULL 时静默返回
 */
void bm_power_converter_exec_current(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_power_converter_current_step(
            (bm_power_converter_axis_t *)instance->state);
    }
}

/**
 * @brief exec_ops init 包装：校验配置并复位轴
 *
 * @param instance exec 实例指针，NULL 或 state 为 NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 参数/配置非法
 */
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

/**
 * @brief exec_ops start 包装：当前无启动动作，直接返回 BM_OK
 *
 * @param instance exec 实例指针（未使用）
 * @return 始终返回 BM_OK
 */
int bm_power_converter_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec_ops safe_stop 包装：清零电流参考并将占空比降至 duty_min
 *
 * @param instance exec 实例指针，NULL 或 state 为 NULL 时静默返回
 */
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

/** @brief 功率变换器 exec_ops 表（电流快环） */
const bm_exec_ops_t bm_power_converter_exec_ops = {
    bm_power_converter_exec_init,
    bm_power_converter_exec_start,
    bm_power_converter_exec_safe_stop
};

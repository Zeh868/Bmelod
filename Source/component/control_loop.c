/**
 * @file control_loop.c
 * @brief 串级 PI 控制环实现
 *
 * 外环 PI 输出作为内环设定，经饱和后驱动 plant 读回与执行器写入。
 * 提供 bm_exec_ops_t 标准封装（exec_init/exec_start/exec_safe_stop + ops 表），
 * 可直接接入调度框架；使能门控仅在绑定 read_command 后生效（默认未使能，
 * 须 CMD_ENABLED）；未接命令通道保持恒使能 legacy 语义。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.6
 * @date 2026-08-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始 K1 骨架
 * 2026-06-23       0.2            zeh            补 bm_exec_ops_t 标准调度封装接口
 * 2026-07-27       0.3            zeh            新增 bm_control_loop_init 四段式入口；exec_init 复用之
 * 2026-07-27       0.4            zeh            init/validate 复用 bm_component_common.h 公共宏
 * 2026-08-01       0.4            zeh           补全 Doxygen 合规注释
 * 2026-08-01       0.5            zeh            对齐 power_control：CMD_ENABLED/FAULT 状态机
 * 2026-08-02       0.6            zeh            使能门控改绑 read_command 是否接入：未接命令通道
 *                                                （read_command==NULL）恢复 0.4 前恒使能语义，修复
 *                                                0.5 对既有消费方的静默零输出破坏；绑定通道仍未使能
 *                                                时 log-once 告警；fault 锁存维持无条件生效
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/control_loop.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_log.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

/** @brief "绑定命令通道却从未使能"告警的 log-once 标志（配置错误类诊断，
 *  多实例共享一次告警足够定位）。 */
static int s_not_enabled_warned = 0;

/**
 * @brief 判定本拍是否允许跑环（使能门控）
 *
 * 门控仅在选择命令通道模型时生效：`resources.read_command` 绑定（非
 * NULL）表示消费方选择"命令驱动使能"，此时须 CMD_ENABLED 才跑环
 * （故障安全默认）；read_command 为 NULL 表示未接命令通道，保持
 * 2026-08-01（v0.5）前的恒使能 legacy 语义，兼容既有消费方。
 * fault_latched 与此无关，在调用点单独无条件判定。
 *
 * @param axis 实例指针
 * @return 非零 允许跑环；0 抑制输出（绑定了通道却从未使能时 log-once）
 */
static int axis_enabled(const bm_control_loop_axis_t *axis) {
    if (axis->resources.read_command == NULL) {
        return 1;
    }
    if ((axis->state.cmd.status & BM_CONTROL_LOOP_CMD_ENABLED) != 0u) {
        return 1;
    }
    if (!s_not_enabled_warned) {
        s_not_enabled_warned = 1;
        BM_LOGW("control_loop",
                "command channel bound but CMD_ENABLED never applied; "
                "output suppressed (bind read_command=NULL for legacy "
                "always-enabled semantics)");
    }
    return 0;
}

/**
 * @brief 锁存故障并清零输出、复位两级积分器
 *
 * @param axis 实例指针
 */
static void latch_fault(bm_control_loop_axis_t *axis) {
    bm_control_loop_state_t *st = &axis->state;

    if (!st->fault_latched) {
        st->fault_latched = 1;
    }
    st->outer_out = 0.0f;
    st->inner_out = 0.0f;
    bm_algo_pi_reset(&st->outer_pi, 0.0f);
    bm_algo_pi_reset(&st->inner_pi, 0.0f);
    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           0.0f);
    }
}

/**
 * @brief 从回调读取最新命令并应用
 *
 * @param axis 实例指针
 */
static void sync_command(bm_control_loop_axis_t *axis) {
    bm_control_loop_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_control_loop_apply_command(axis, &command);
    }
}

int bm_control_loop_validate_config(const bm_control_loop_config_t *config) {
    BM_COMPONENT_RETURN_IF_NULL(config);
    BM_COMPONENT_VALIDATE_POSITIVE_FLOAT(config->dt_s);
    if (bm_algo_pi_validate_config(&config->outer_pi) != BM_OK ||
        bm_algo_pi_validate_config(&config->inner_pi) != BM_OK) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_control_loop_reset(bm_control_loop_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_pi_reset(&axis->state.outer_pi, 0.0f);
    bm_algo_pi_reset(&axis->state.inner_pi, 0.0f);
    axis->state.outer_out = 0.0f;
    axis->state.inner_out = 0.0f;
    axis->state.step_count = 0u;
    axis->state.fault_latched = 0;
    memset(&axis->state.cmd, 0, sizeof(axis->state.cmd));
}

int bm_control_loop_init(bm_control_loop_axis_t *axis) {
    BM_COMPONENT_INIT(axis, bm_control_loop_validate_config,
                      bm_control_loop_reset);
}

void bm_control_loop_apply_command(bm_control_loop_axis_t *axis,
                                   const bm_control_loop_cmd_t *cmd) {
    if (axis == NULL || cmd == NULL) {
        return;
    }

    axis->state.cmd = *cmd;
    if ((cmd->status & BM_CONTROL_LOOP_CMD_FAULT) != 0u) {
        latch_fault(axis);
    }
}

void bm_control_loop_step(bm_control_loop_axis_t *axis) {
    const bm_control_loop_config_t *cfg;
    bm_control_loop_state_t *st;
    float outer_meas = 0.0f;
    float inner_meas = 0.0f;
    float setpoint = 0.0f;
    float outer_err;
    float inner_err;

    if (axis == NULL ||
        bm_control_loop_validate_config(&axis->config) != BM_OK) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    sync_command(axis);

    if (st->fault_latched || !axis_enabled(axis)) {
        st->outer_out = 0.0f;
        st->inner_out = 0.0f;
        bm_algo_pi_reset(&st->outer_pi, 0.0f);
        bm_algo_pi_reset(&st->inner_pi, 0.0f);
        if (axis->resources.write_output != NULL) {
            (void)axis->resources.write_output(
                axis->resources.write_output_user, 0.0f);
        }
        return;
    }

    if (axis->resources.read_plant != NULL &&
        axis->resources.read_plant(axis->resources.read_plant_user,
                                   &outer_meas, &inner_meas,
                                   &setpoint) != 0) {
        latch_fault(axis);
        return;
    }

    outer_err = setpoint - outer_meas;
    st->outer_out = bm_algo_pi_step(&st->outer_pi, &cfg->outer_pi,
                                    outer_err, cfg->dt_s);

    inner_err = st->outer_out - inner_meas;
    st->inner_out = bm_algo_pi_step(&st->inner_pi, &cfg->inner_pi,
                                    inner_err, cfg->dt_s);

    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           st->inner_out);
    }
    st->step_count++;
}

/* ---------------------------------------------------------------------------
 * bm_exec_ops_t 标准封装
 * 语义与 power_control 组件保持一致：
 *   exec_init  → 校验配置 + 复位状态
 *   exec_start → 无额外操作（保留扩展点）
 *   exec_step  → 转发 bm_control_loop_step()（由调度槽 run 回调调用）
 *   exec_safe_stop → 输出归零 + 复位两级积分器
 * ---------------------------------------------------------------------------
 */

/**
 * @brief exec 封装：运行一步串级 PI
 *
 * 通过 instance->state 取得 bm_control_loop_axis_t 指针后调用
 * bm_control_loop_step()，行为与直接调用完全一致。
 *
 * @param instance exec 实例指针，instance->state 须为 bm_control_loop_axis_t*
 */
void bm_control_loop_exec_step(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_control_loop_step((bm_control_loop_axis_t *)instance->state);
    }
}

/**
 * @brief exec 生命周期：初始化
 *
 * 校验配置合法性，并将所有状态复位为零初值。
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_control_loop_exec_init(const bm_exec_t *instance) {
    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    return bm_control_loop_init((bm_control_loop_axis_t *)instance->state);
}

/**
 * @brief exec 生命周期：启动
 *
 * 当前无额外操作，保留供后续扩展（如使能执行器使能信号）。
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_control_loop_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec 生命周期：安全停机
 *
 * 将内外环输出归零，复位两级 PI 积分器，并通过 write_output 回调向
 * 执行器写入零值，确保停机时执行器处于安全状态。
 *
 * @param instance exec 实例指针
 */
void bm_control_loop_exec_safe_stop(const bm_exec_t *instance) {
    bm_control_loop_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_control_loop_axis_t *)instance->state;
    axis->state.outer_out = 0.0f;
    axis->state.inner_out = 0.0f;
    bm_algo_pi_reset(&axis->state.outer_pi, 0.0f);
    bm_algo_pi_reset(&axis->state.inner_pi, 0.0f);
    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           0.0f);
    }
}

/**
 * @brief control_loop 标准 exec ops 表
 *
 * 将此指针赋给 bm_exec_t::ops，即可将 control_loop 实例
 * 接入调度框架的生命周期管理。
 */
const bm_exec_ops_t bm_control_loop_exec_ops = {
    bm_control_loop_exec_init,
    bm_control_loop_exec_start,
    bm_control_loop_exec_safe_stop
};

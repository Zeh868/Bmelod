/**
 * @file grid_control.c
 * @brief SOGI-PLL + PR 电流环并网控制实现
 *
 * 封装 SOGI-PLL 锁相与 PR 谐振电流环，并提供 bm_exec_ops_t 调度封装。
 * 使能门控仅在绑定 read_command 后生效（默认未使能，须 CMD_ENABLED）；
 * 未接命令通道保持恒使能 legacy 语义。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.5
 * @date 2026-08-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 validate_config PLL/PR 参数校验；补 exec_ops 封装
 * 2026-08-01       0.2            zeh           补全 Doxygen 合规注释
 * 2026-08-01       0.3            zeh            对齐 power_control：CMD_ENABLED/FAULT 状态机
 * 2026-08-01       0.4            zeh            exec_safe_stop 复位 PLL/PR，对齐 power_control
 * 2026-08-02       0.5            zeh            使能门控改绑 read_command 是否接入：未接命令通道
 *                                                （read_command==NULL）恢复 0.2 前恒使能语义，修复
 *                                                0.3 对既有消费方的静默零输出破坏；绑定通道仍未使能
 *                                                时 log-once 告警；fault 锁存维持无条件生效
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/grid_control.h"
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
 * 2026-08-01（v0.3）前的恒使能 legacy 语义，兼容既有消费方。
 * fault_latched 与此无关，在调用点单独无条件判定。
 *
 * @param axis 实例指针
 * @return 非零 允许跑环；0 抑制输出（绑定了通道却从未使能时 log-once）
 */
static int axis_enabled(const bm_grid_control_axis_t *axis) {
    if (axis->resources.read_command == NULL) {
        return 1;
    }
    if ((axis->state.cmd.status & BM_GRID_CTRL_CMD_ENABLED) != 0u) {
        return 1;
    }
    if (!s_not_enabled_warned) {
        s_not_enabled_warned = 1;
        BM_LOGW("grid_control",
                "command channel bound but CMD_ENABLED never applied; "
                "output suppressed (bind read_command=NULL for legacy "
                "always-enabled semantics)");
    }
    return 0;
}

/**
 * @brief 锁存故障并清零 v_cmd、复位 PLL/PR
 *
 * @param axis 实例指针
 */
static void latch_fault(bm_grid_control_axis_t *axis) {
    bm_grid_control_state_t *st = &axis->state;

    if (!st->fault_latched) {
        st->fault_latched = 1;
    }
    st->v_cmd = 0.0f;
    bm_algo_sogi_pll_reset(&st->pll, &axis->config.pll);
    bm_algo_pr_reset(&st->pr_current);
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
static void sync_command(bm_grid_control_axis_t *axis) {
    bm_grid_ctrl_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_grid_control_apply_command(axis, &command);
    }
}

int bm_grid_control_validate_config(const bm_grid_control_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* PLL：额定角频率须为正值（典型 2π×50≈314.16 rad/s） */
    if (config->pll.nominal_omega_rad_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* SOGI 增益 k_sogi 须为正值（典型 √2≈1.414） */
    if (config->pll.k_sogi <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* PLL 比例增益 k_pll 须为正值 */
    if (config->pll.k_pll <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* PR 控制器谐振角频率须为正值 */
    if (config->pr_current.omega_rad_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* PR kp 须为非负（允许纯谐振控制器 kp=0） */
    if (config->pr_current.kp < 0.0f) {
        return BM_ERR_INVALID;
    }
    /* PR 谐振增益 kr 须为正值 */
    if (config->pr_current.kr <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* PR 输出限幅须合法 */
    if (config->pr_current.out_max <= config->pr_current.out_min) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_grid_control_reset(bm_grid_control_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_sogi_pll_reset(&axis->state.pll, &axis->config.pll);
    bm_algo_pr_reset(&axis->state.pr_current);
    axis->state.theta_rad = 0.0f;
    axis->state.omega_rad_s = axis->config.pll.nominal_omega_rad_s;
    axis->state.v_cmd = 0.0f;
    axis->state.step_count = 0u;
    axis->state.fault_latched = 0;
    memset(&axis->state.cmd, 0, sizeof(axis->state.cmd));
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_grid_control_init(bm_grid_control_axis_t *axis) {
    if (axis == NULL ||
        bm_grid_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_pr_init(&axis->state.pr_current, &axis->config.pr_current,
                        axis->config.dt_s) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_pr_compute_coeffs(&axis->config.pr_current, axis->config.dt_s,
                                  &axis->state.pr_b0, &axis->state.pr_b1,
                                  &axis->state.pr_b2, &axis->state.pr_a1,
                                  &axis->state.pr_a2) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_grid_control_reset(axis);
    return BM_OK;
}

void bm_grid_control_apply_command(bm_grid_control_axis_t *axis,
                                   const bm_grid_ctrl_cmd_t *cmd) {
    if (axis == NULL || cmd == NULL) {
        return;
    }

    axis->state.cmd = *cmd;
    if ((cmd->status & BM_GRID_CTRL_CMD_FAULT) != 0u) {
        latch_fault(axis);
    }
}

void bm_grid_control_step(bm_grid_control_axis_t *axis) {
    const bm_grid_control_config_t *cfg;
    bm_grid_control_state_t *st;
    float v_grid = 0.0f;
    float i_meas = 0.0f;
    float i_ref = 0.0f;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    sync_command(axis);

    if (st->fault_latched || !axis_enabled(axis)) {
        st->v_cmd = 0.0f;
        bm_algo_sogi_pll_reset(&st->pll, &cfg->pll);
        bm_algo_pr_reset(&st->pr_current);
        if (axis->resources.write_output != NULL) {
            (void)axis->resources.write_output(
                axis->resources.write_output_user, 0.0f);
        }
        if (st->fault_latched) {
            st->telemetry.status = BM_GRID_CTRL_TEL_FAULT;
            st->telemetry.v_cmd = 0.0f;
            BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
        }
        return;
    }

    if (axis->resources.read_io != NULL &&
        axis->resources.read_io(axis->resources.read_io_user,
                                &v_grid, &i_meas, &i_ref) != 0) {
        latch_fault(axis);
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_GRID_CTRL_TEL_STALE | BM_GRID_CTRL_TEL_FAULT;
        st->telemetry.theta_rad = st->theta_rad;
        st->telemetry.omega_rad_s = st->omega_rad_s;
        st->telemetry.i_ref_a = i_ref;
        st->telemetry.i_meas_a = i_meas;
        st->telemetry.v_cmd = 0.0f;
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
        return;
    }

    bm_algo_sogi_pll_step(&st->pll, &cfg->pll, v_grid, cfg->dt_s);
    st->theta_rad = st->pll.theta_rad;
    st->omega_rad_s = st->pll.omega_rad_s;

    st->v_cmd = bm_algo_pr_step(&st->pr_current, &cfg->pr_current,
                                i_ref - i_meas,
                                st->pr_b0, st->pr_b1, st->pr_b2,
                                st->pr_a1, st->pr_a2);

    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           st->v_cmd);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_GRID_CTRL_TEL_VALID;
    st->telemetry.theta_rad = st->theta_rad;
    st->telemetry.omega_rad_s = st->omega_rad_s;
    st->telemetry.i_ref_a = i_ref;
    st->telemetry.i_meas_a = i_meas;
    st->telemetry.v_cmd = st->v_cmd;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/* ---------- exec_ops 封装 ---------- */

void bm_grid_control_exec_run(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_grid_control_step((bm_grid_control_axis_t *)instance->state);
    }
}

int bm_grid_control_exec_init(const bm_exec_t *instance) {
    bm_grid_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_grid_control_axis_t *)instance->state;
    return bm_grid_control_init(axis);
}

int bm_grid_control_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_grid_control_exec_safe_stop(const bm_exec_t *instance) {
    bm_grid_control_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_grid_control_axis_t *)instance->state;
    /* 清输出并复位 PLL/PR，避免停机后重启残留积分/谐振；不清 cmd/fault */
    axis->state.v_cmd = 0.0f;
    bm_algo_sogi_pll_reset(&axis->state.pll, &axis->config.pll);
    bm_algo_pr_reset(&axis->state.pr_current);
    if (axis->resources.write_output != NULL) {
        (void)axis->resources.write_output(axis->resources.write_output_user,
                                           0.0f);
    }
}

const bm_exec_ops_t bm_grid_control_exec_ops = {
    bm_grid_control_exec_init,
    bm_grid_control_exec_start,
    bm_grid_control_exec_safe_stop
};

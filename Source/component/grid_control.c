/**
 * @file grid_control.c
 * @brief SOGI-PLL + PR 电流环并网控制实现
 *
 * 封装 SOGI-PLL 锁相与 PR 谐振电流环，并提供 bm_exec_ops_t 调度封装。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 validate_config PLL/PR 参数校验；补 exec_ops 封装
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/grid_control.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

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

    if (axis->resources.read_io != NULL &&
        axis->resources.read_io(axis->resources.read_io_user,
                                &v_grid, &i_meas, &i_ref) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_GRID_CTRL_TEL_STALE;
        st->telemetry.theta_rad = st->theta_rad;
        st->telemetry.omega_rad_s = st->omega_rad_s;
        st->telemetry.i_ref_a = i_ref;
        st->telemetry.i_meas_a = i_meas;
        st->telemetry.v_cmd = 0.0f;
        st->v_cmd = 0.0f;
        if (axis->resources.write_output != NULL) {
            (void)axis->resources.write_output(
                axis->resources.write_output_user, 0.0f);
        }
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
    /* 安全停止：清零 v_cmd，写入硬件，停止调制 */
    axis->state.v_cmd = 0.0f;
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

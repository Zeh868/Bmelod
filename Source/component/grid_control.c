/**
 * @file grid_control.c
 * @brief SOGI-PLL + PR 电流环并网控制实现
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
#include "bm/component/grid_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_grid_control_validate_config(const bm_grid_control_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
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
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
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

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

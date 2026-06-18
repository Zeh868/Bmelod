/**
 * @file control_loop.c
 * @brief 串级 PI 控制环实现
 *
 * 外环 PI 输出作为内环设定，经饱和后驱动 plant 读回与执行器写入。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始 K1 骨架
 */
#include "bm/component/control_loop.h"
#include "bm/common/bm_types.h"

int bm_control_loop_validate_config(const bm_control_loop_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_pi_validate_config(&config->outer_pi) != 0 ||
        bm_algo_pi_validate_config(&config->inner_pi) != 0) {
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

    if (axis->resources.read_plant != NULL &&
        axis->resources.read_plant(axis->resources.read_plant_user,
                                   &outer_meas, &inner_meas,
                                   &setpoint) != 0) {
        st->step_count++;
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

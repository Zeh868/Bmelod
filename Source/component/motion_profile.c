/**
 * @file motion_profile.c
 * @brief 单轴运动轨迹规划实现
 *
 * 复用 bm_algo_profile 梯形或 S 曲线核，按周期输出位置与速度。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#include "bm/component/motion_profile.h"
#include "bm/common/bm_types.h"

int bm_motion_profile_validate_config(const bm_motion_profile_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->vmax <= 0.0f || config->amax <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->type == BM_MOTION_PROFILE_SCURVE && config->jerk <= 0.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_motion_profile_reset(bm_motion_profile_axis_t *axis, float position) {
    if (axis == NULL) {
        return;
    }

    bm_algo_trapezoid_reset(&axis->state.trapezoid, position, 0.0f);
    bm_algo_scurve_reset(&axis->state.scurve, position, 0.0f, 0.0f);
    axis->state.target_pos = position;
    axis->state.active = 0;
    axis->state.step_count = 0u;
}

void bm_motion_profile_goto(bm_motion_profile_axis_t *axis, float position) {
    if (axis == NULL) {
        return;
    }

    axis->state.target_pos = position;
    axis->state.active = 1;
    if (axis->config.type == BM_MOTION_PROFILE_SCURVE) {
        bm_algo_scurve_set_target(&axis->state.scurve, position);
    } else {
        bm_algo_trapezoid_set_target(&axis->state.trapezoid, position);
    }
}

void bm_motion_profile_step(bm_motion_profile_axis_t *axis,
                            bm_motion_profile_output_t *out) {
    const bm_motion_profile_config_t *cfg;
    bm_motion_profile_state_t *st;
    bm_algo_trapezoid_config_t trap_cfg;
    bm_algo_scurve_config_t scurve_cfg;

    if (axis == NULL || out == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (!st->active) {
        out->position = (cfg->type == BM_MOTION_PROFILE_SCURVE) ?
                        st->scurve.position : st->trapezoid.position;
        out->velocity = (cfg->type == BM_MOTION_PROFILE_SCURVE) ?
                        st->scurve.velocity : st->trapezoid.velocity;
        out->done = 1;
        return;
    }

    if (cfg->type == BM_MOTION_PROFILE_SCURVE) {
        scurve_cfg.max_vel = cfg->vmax;
        scurve_cfg.max_accel = cfg->amax;
        scurve_cfg.max_jerk = cfg->jerk;
        (void)bm_algo_scurve_step(&st->scurve, &scurve_cfg, cfg->dt_s);
        out->position = st->scurve.position;
        out->velocity = st->scurve.velocity;
        out->done = st->scurve.done;
    } else {
        trap_cfg.max_vel = cfg->vmax;
        trap_cfg.max_accel = cfg->amax;
        trap_cfg.max_decel = cfg->amax;
        (void)bm_algo_trapezoid_step(&st->trapezoid, &trap_cfg, cfg->dt_s);
        out->position = st->trapezoid.position;
        out->velocity = st->trapezoid.velocity;
        out->done = st->trapezoid.done;
    }

    if (out->done) {
        st->active = 0;
    }
    st->step_count++;
}

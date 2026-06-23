/**
 * @file motion_profile.c
 * @brief 单轴运动轨迹规划实现
 *
 * 复用 bm_algo_profile 梯形或 S 曲线核，按周期输出位置与速度，
 * 并提供 bm_exec_ops_t 调度封装以接入 bm_exec 生命周期管理。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 NULL 保护（validate/goto/step）；exec_ops；Doxygen；SPDX
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
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
    /* 补 validate_config NULL 保护：配置非法时不启动运动 */
    if (bm_motion_profile_validate_config(&axis->config) != BM_OK) {
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
    /* 配置非法时静默返回，避免除零或越界 */
    if (bm_motion_profile_validate_config(&axis->config) != BM_OK) {
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

/* ---------- exec_ops 封装 ---------- */

void bm_motion_profile_exec_run(const bm_exec_t *instance) {
    /* exec_run 需要输出缓冲区；此槽仅驱动内部状态更新，out 由调用方轮询 */
    bm_motion_profile_output_t out;

    if (instance != NULL && instance->state != NULL) {
        bm_motion_profile_step(
            (bm_motion_profile_axis_t *)instance->state, &out);
    }
}

int bm_motion_profile_exec_init(const bm_exec_t *instance) {
    bm_motion_profile_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_motion_profile_axis_t *)instance->state;
    if (bm_motion_profile_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_motion_profile_reset(axis, 0.0f);
    return BM_OK;
}

int bm_motion_profile_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_motion_profile_exec_safe_stop(const bm_exec_t *instance) {
    bm_motion_profile_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_motion_profile_axis_t *)instance->state;
    /* 安全停止：清除运动激活标志，停止轨迹输出 */
    axis->state.active = 0;
}

const bm_exec_ops_t bm_motion_profile_exec_ops = {
    bm_motion_profile_exec_init,
    bm_motion_profile_exec_start,
    bm_motion_profile_exec_safe_stop
};

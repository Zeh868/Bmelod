/**
 * @file motion_coordination.c
 * @brief 多轴斜坡协调组件实现
 *
 * 最多 BM_MOTION_COORD_MAX_AXES 轴同步斜坡协调。
 * exec_ops 封装提供 bm_exec 周期调度接入点。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 exec_ops 封装；Doxygen；SPDX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/motion_coordination.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

int bm_motion_coordination_validate_config(
    const bm_motion_coordination_config_t *config) {
    uint32_t i;

    if (config == NULL || config->dt_s <= 0.0f ||
        config->axis_count == 0u ||
        config->axis_count > BM_MOTION_COORD_MAX_AXES) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < config->axis_count; i++) {
        if (config->ramp[i].rate_per_s <= 0.0f) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

void bm_motion_coordination_reset(bm_motion_coordination_axis_t *axis,
                                  const float *initial) {
    uint32_t i;
    uint32_t n;

    if (axis == NULL) {
        return;
    }

    n = axis->config.axis_count;
    for (i = 0u; i < n; i++) {
        float pos = (initial != NULL) ? initial[i] : 0.0f;
        bm_algo_ramp_reset(&axis->state.ramp[i], pos);
        axis->state.target[i] = pos;
    }
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    axis->state.telemetry.axis_count = n;
}

int bm_motion_coordination_init(bm_motion_coordination_axis_t *axis) {
    if (axis == NULL ||
        bm_motion_coordination_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_motion_coordination_reset(axis, NULL);
    return BM_OK;
}

void bm_motion_coordination_set_targets(bm_motion_coordination_axis_t *axis,
                                        const float *targets) {
    uint32_t i;

    if (axis == NULL || targets == NULL) {
        return;
    }
    for (i = 0u; i < axis->config.axis_count; i++) {
        axis->state.target[i] = targets[i];
    }
}

void bm_motion_coordination_step(bm_motion_coordination_axis_t *axis) {
    const bm_motion_coordination_config_t *cfg;
    bm_motion_coordination_state_t *st;
    uint32_t i;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    for (i = 0u; i < cfg->axis_count; i++) {
        st->telemetry.position[i] =
            bm_algo_ramp_step(&st->ramp[i], &cfg->ramp[i],
                              st->target[i], cfg->dt_s);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_MOTION_COORD_TEL_VALID;
    st->telemetry.axis_count = cfg->axis_count;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/* ---------- exec_ops 封装 ---------- */

/**
 * @brief exec_ops.run 转发：调用 bm_motion_coordination_step
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_motion_coordination_axis_t
 */
void bm_motion_coordination_exec_run(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_motion_coordination_step(
            (bm_motion_coordination_axis_t *)instance->state);
    }
}

/**
 * @brief exec_ops.init 实现：校验配置并以全零复位各轴
 *
 * @param instance bm_exec 实例
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法或配置校验失败
 */
int bm_motion_coordination_exec_init(const bm_exec_t *instance) {
    bm_motion_coordination_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_motion_coordination_axis_t *)instance->state;
    if (bm_motion_coordination_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_motion_coordination_reset(axis, NULL);
    return BM_OK;
}

/**
 * @brief exec_ops.start 实现：无额外启动动作，始终返回 BM_OK
 *
 * @param instance bm_exec 实例（未使用）
 * @return BM_OK
 */
int bm_motion_coordination_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec_ops.safe_stop 实现：将各轴 target 锁定到当前输出位置（就地停止）
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_motion_coordination_axis_t
 */
void bm_motion_coordination_exec_safe_stop(const bm_exec_t *instance) {
    bm_motion_coordination_axis_t *axis;
    uint32_t i;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_motion_coordination_axis_t *)instance->state;
    for (i = 0u; i < axis->config.axis_count; i++) {
        axis->state.target[i] = axis->state.ramp[i].output;
    }
}

/** @brief motion_coordination 标准 exec 生命周期操作表 */
const bm_exec_ops_t bm_motion_coordination_exec_ops = {
    bm_motion_coordination_exec_init,
    bm_motion_coordination_exec_start,
    bm_motion_coordination_exec_safe_stop
};

/**
 * @file motion_coordination.c
 * @brief 多轴斜坡协调组件实现
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
#include "bm/component/motion_coordination.h"
#include "bm/common/bm_types.h"

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

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

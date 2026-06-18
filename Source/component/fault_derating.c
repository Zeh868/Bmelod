/**
 * @file fault_derating.c
 * @brief 故障锁存与线性降额组件实现
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
#include "bm/component/fault_derating.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_fault_derating_validate_config(const bm_fault_derating_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->derate_ramp.rate_per_s <= 0.0f ||
        config->recovery_time_s < 0.0f ||
        config->derate_target < 0.0f || config->derate_target > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_fault_derating_init(bm_fault_derating_axis_t *axis) {
    if (axis == NULL ||
        bm_fault_derating_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_fault_derating_reset(axis);
    return BM_OK;
}

void bm_fault_derating_reset(bm_fault_derating_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_ramp_reset(&axis->state.derate_ramp, 1.0f);
    axis->state.fault_latched = 0;
    axis->state.derate_factor = 1.0f;
    axis->state.recovery_elapsed_s = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    axis->state.telemetry.derate_factor = 1.0f;
}

void bm_fault_derating_latch(bm_fault_derating_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    axis->state.fault_latched = 1;
    axis->state.recovery_elapsed_s = 0.0f;
}

void bm_fault_derating_clear_request(bm_fault_derating_axis_t *axis) {
    if (axis == NULL || !axis->state.fault_latched) {
        return;
    }
    axis->state.fault_latched = 0;
    axis->state.recovery_elapsed_s = 0.0f;
}

void bm_fault_derating_step(bm_fault_derating_axis_t *axis) {
    const bm_fault_derating_config_t *cfg;
    bm_fault_derating_state_t *st;
    float target;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (st->fault_latched) {
        target = cfg->derate_target;
        st->derate_factor = bm_algo_ramp_step(&st->derate_ramp, &cfg->derate_ramp,
                                              target, cfg->dt_s);
    } else {
        st->recovery_elapsed_s += cfg->dt_s;
        if (st->recovery_elapsed_s >= cfg->recovery_time_s) {
            target = 1.0f;
            st->derate_factor = bm_algo_ramp_step(&st->derate_ramp, &cfg->derate_ramp,
                                                  target, cfg->dt_s);
        }
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_FAULT_DERATING_TEL_VALID;
    if (st->fault_latched) {
        st->telemetry.status |= BM_FAULT_DERATING_TEL_LATCHED;
    }
    st->telemetry.derate_factor = st->derate_factor;
    st->telemetry.recovery_elapsed_s = st->recovery_elapsed_s;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

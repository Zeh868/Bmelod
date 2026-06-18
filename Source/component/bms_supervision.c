/**
 * @file bms_supervision.c
 * @brief BMS Pack 监督与降额集成实现
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
#include "bm/component/bms_supervision.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static void bms_supervision_sync_derating_config(
    bm_bms_supervision_axis_t *axis) {
    axis->state.derating.config.derate_ramp = axis->config.derate_ramp;
    axis->state.derating.config.recovery_time_s = axis->config.recovery_time_s;
    axis->state.derating.config.derate_target = axis->config.derate_target;
    axis->state.derating.config.dt_s = axis->config.dt_s;
}

int bm_bms_supervision_validate_config(const bm_bms_supervision_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->v_max_v <= config->v_min_v || config->i_max_a <= 0.0f ||
        config->derate_ramp.rate_per_s <= 0.0f ||
        config->recovery_time_s < 0.0f ||
        config->derate_target < 0.0f || config->derate_target > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_bms_supervision_reset(bm_bms_supervision_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_fault_derating_reset(&axis->state.derating);
    axis->state.limit_flags = 0u;
    axis->state.pack_voltage_v = 0.0f;
    axis->state.pack_current_a = 0.0f;
    axis->state.temp_c = 25.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_bms_supervision_init(bm_bms_supervision_axis_t *axis) {
    if (axis == NULL ||
        bm_bms_supervision_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bms_supervision_sync_derating_config(axis);
    if (bm_fault_derating_init(&axis->state.derating) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_bms_supervision_reset(axis);
    return BM_OK;
}

void bm_bms_supervision_step(bm_bms_supervision_axis_t *axis) {
    const bm_bms_supervision_config_t *cfg;
    bm_bms_supervision_state_t *st;
    uint32_t flags = 0u;
    float voltage_v = 0.0f;
    float current_a = 0.0f;
    float temp_c = 25.0f;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_sample != NULL &&
        axis->resources.read_sample(axis->resources.read_sample_user,
                                  &voltage_v, &current_a, &temp_c) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_BMS_SUP_TEL_STALE;
        st->telemetry.pack_voltage_v = st->pack_voltage_v;
        st->telemetry.pack_current_a = st->pack_current_a;
        st->telemetry.temp_c = st->temp_c;
        st->telemetry.derate_factor = st->derating.state.derate_factor;
        st->telemetry.limit_flags = st->limit_flags;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    st->pack_voltage_v = voltage_v;
    st->pack_current_a = current_a;
    st->temp_c = temp_c;

    if (voltage_v > cfg->v_max_v) {
        flags |= BM_BMS_SUP_LIMIT_VOLTAGE_HIGH;
    }
    if (voltage_v < cfg->v_min_v) {
        flags |= BM_BMS_SUP_LIMIT_VOLTAGE_LOW;
    }
    if (fabsf(current_a) > cfg->i_max_a) {
        flags |= BM_BMS_SUP_LIMIT_CURRENT;
    }
    if (temp_c > cfg->temp_max_c) {
        flags |= BM_BMS_SUP_LIMIT_TEMP;
    }

    st->limit_flags = flags;
    if (flags != 0u) {
        bm_fault_derating_latch(&st->derating);
    } else {
        bm_fault_derating_clear_request(&st->derating);
    }

    bms_supervision_sync_derating_config(axis);
    bm_fault_derating_step(&st->derating);

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_BMS_SUP_TEL_VALID;
    if (st->derating.state.derate_factor < 1.0f) {
        st->telemetry.status |= BM_BMS_SUP_TEL_DERATED;
    }
    st->telemetry.pack_voltage_v = voltage_v;
    st->telemetry.pack_current_a = current_a;
    st->telemetry.temp_c = temp_c;
    st->telemetry.derate_factor = st->derating.state.derate_factor;
    st->telemetry.limit_flags = flags;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

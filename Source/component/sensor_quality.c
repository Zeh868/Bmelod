/**
 * @file sensor_quality.c
 * @brief 传感器信号质量监控组件实现
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
#include "bm/component/sensor_quality.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

int bm_sensor_quality_validate_config(const bm_sensor_quality_config_t *config) {
    if (config == NULL || config->dt_s <= 0.0f ||
        config->monitor.min_v >= config->monitor.max_v) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_sensor_quality_reset(bm_sensor_quality_axis_t *axis, float initial) {
    if (axis == NULL) {
        return;
    }

    bm_algo_range_monitor_reset(&axis->state.monitor, initial);
    axis->state.frozen_prev = initial;
    axis->state.frozen_count = 0u;
    axis->state.fault_flags = 0u;
    axis->state.last_value = initial;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    axis->state.telemetry.value = initial;
}

int bm_sensor_quality_init(bm_sensor_quality_axis_t *axis, float initial) {
    if (axis == NULL ||
        bm_sensor_quality_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_sensor_quality_reset(axis, initial);
    return BM_OK;
}

void bm_sensor_quality_step(bm_sensor_quality_axis_t *axis) {
    const bm_sensor_quality_config_t *cfg;
    bm_sensor_quality_state_t *st;
    float sample = 0.0f;
    uint32_t flags;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_sample != NULL &&
        axis->resources.read_sample(axis->resources.read_sample_user,
                                  &sample) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_SENSOR_QUALITY_TEL_STALE;
        st->telemetry.value = st->last_value;
        st->telemetry.fault_flags = st->fault_flags;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    flags = bm_algo_range_monitor_step(&st->monitor, &cfg->monitor,
                                       sample, cfg->dt_s);

    if (fabsf(sample - st->frozen_prev) <= cfg->frozen_epsilon) {
        if (st->frozen_count < cfg->frozen_count_required) {
            st->frozen_count++;
        }
        if (st->frozen_count >= cfg->frozen_count_required &&
            cfg->frozen_count_required > 0u &&
            st->step_count > 0u) {
            flags |= BM_ALGO_FAULT_FROZEN;
        }
    } else {
        st->frozen_count = 0u;
        st->frozen_prev = sample;
    }

    st->fault_flags = flags;
    st->last_value = sample;
    st->step_count++;

    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_SENSOR_QUALITY_TEL_VALID;
    st->telemetry.value = sample;
    st->telemetry.fault_flags = flags;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

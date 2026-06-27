/**
 * @file sensor_quality.c
 * @brief 传感器信号质量监控组件实现
 *
 * 三级质量检查：范围越限、变化率超限、冻结值检测。
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

/* ---------- exec_ops 封装 ---------- */

/**
 * @brief exec_ops.run 转发：调用 bm_sensor_quality_step
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_sensor_quality_axis_t
 */
void bm_sensor_quality_exec_run(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_sensor_quality_step((bm_sensor_quality_axis_t *)instance->state);
    }
}

/**
 * @brief exec_ops.init 实现：校验配置并以 0.0f 为初始值复位状态
 *
 * @param instance bm_exec 实例
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法或配置校验失败
 */
int bm_sensor_quality_exec_init(const bm_exec_t *instance) {
    bm_sensor_quality_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_sensor_quality_axis_t *)instance->state;
    if (bm_sensor_quality_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_sensor_quality_reset(axis, 0.0f);
    return BM_OK;
}

/**
 * @brief exec_ops.start 实现：无额外启动动作，始终返回 BM_OK
 *
 * @param instance bm_exec 实例（未使用）
 * @return BM_OK
 */
int bm_sensor_quality_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec_ops.safe_stop 实现：清零故障标志，停止遥测输出
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_sensor_quality_axis_t
 */
void bm_sensor_quality_exec_safe_stop(const bm_exec_t *instance) {
    bm_sensor_quality_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_sensor_quality_axis_t *)instance->state;
    axis->state.fault_flags = 0u;
}

/** @brief sensor_quality 标准 exec 生命周期操作表 */
const bm_exec_ops_t bm_sensor_quality_exec_ops = {
    bm_sensor_quality_exec_init,
    bm_sensor_quality_exec_start,
    bm_sensor_quality_exec_safe_stop
};

/**
 * @file bms_supervision.c
 * @brief BMS Pack 监督与降额集成实现
 *
 * 封装电压/电流/温度越限检测，通过 common 降额服务进行降额，
 * 并提供 bm_exec_ops_t 调度封装以接入 bm_exec 生命周期管理。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.4
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 exec_ops 封装；补全公共函数 Doxygen；SPDX 头
 * 2026-07-27       0.3            zeh            解耦旧降额实现：使用 resources 服务绑定
 *
 * 2026-07-28       0.4            zeh            通过 common 降额服务调用，移除组件直接依赖
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/bms_supervision.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <math.h>
#include <string.h>

/**
 * @brief 检查降额服务绑定是否完整
 *
 * @param service 待检查服务绑定
 * @return 非零表示绑定完整，零表示存在缺项
 */
static int bms_supervision_derating_service_is_valid(
    const bm_derating_service_t *service) {
    return service != NULL && service->context != NULL &&
           service->configure != NULL && service->reset != NULL &&
           service->latch != NULL && service->clear_request != NULL &&
           service->step != NULL && service->get_factor != NULL;
}

/**
 * @brief 将 BMS 降额配置同步至服务实现
 *
 * @param axis BMS 监督轴实例
 * @return 服务 configure 的返回值
 */
static int bms_supervision_sync_derating_config(
    bm_bms_supervision_axis_t *axis) {
    bm_derating_service_config_t config;

    config.rate_per_s = axis->config.derate_ramp.rate_per_s;
    config.recovery_time_s = axis->config.recovery_time_s;
    config.dt_s = axis->config.dt_s;
    config.target_factor = axis->config.derate_target;
    return axis->resources.derating.configure(
        axis->resources.derating.context, &config);
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
    if (axis == NULL ||
        !bms_supervision_derating_service_is_valid(&axis->resources.derating)) {
        return;
    }

    axis->resources.derating.reset(axis->resources.derating.context);
    axis->state.limit_flags = 0u;
    axis->state.pack_voltage_v = 0.0f;
    axis->state.pack_current_a = 0.0f;
    axis->state.temp_c = 25.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_bms_supervision_init(bm_bms_supervision_axis_t *axis) {
    if (axis == NULL ||
        !bms_supervision_derating_service_is_valid(&axis->resources.derating) ||
        bm_bms_supervision_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (bms_supervision_sync_derating_config(axis) != BM_OK) {
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

    if (axis == NULL ||
        !bms_supervision_derating_service_is_valid(&axis->resources.derating)) {
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
        st->telemetry.derate_factor = axis->resources.derating.get_factor(
            axis->resources.derating.context);
        st->telemetry.limit_flags = st->limit_flags;
        BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
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
        axis->resources.derating.latch(axis->resources.derating.context);
    } else {
        axis->resources.derating.clear_request(axis->resources.derating.context);
    }

    if (bms_supervision_sync_derating_config(axis) != BM_OK) {
        return;
    }
    axis->resources.derating.step(axis->resources.derating.context);

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_BMS_SUP_TEL_VALID;
    st->telemetry.derate_factor = axis->resources.derating.get_factor(
        axis->resources.derating.context);
    if (st->telemetry.derate_factor < 1.0f) {
        st->telemetry.status |= BM_BMS_SUP_TEL_DERATED;
    }
    st->telemetry.pack_voltage_v = voltage_v;
    st->telemetry.pack_current_a = current_a;
    st->telemetry.temp_c = temp_c;
    st->telemetry.limit_flags = flags;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/* ---------- exec_ops 封装 ---------- */

void bm_bms_supervision_exec_run(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_bms_supervision_step(
            (bm_bms_supervision_axis_t *)instance->state);
    }
}

int bm_bms_supervision_exec_init(const bm_exec_t *instance) {
    bm_bms_supervision_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_bms_supervision_axis_t *)instance->state;
    return bm_bms_supervision_init(axis);
}

int bm_bms_supervision_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void bm_bms_supervision_exec_safe_stop(const bm_exec_t *instance) {
    bm_bms_supervision_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_bms_supervision_axis_t *)instance->state;
    /* 安全停止：重置降额状态，使对外输出因子恢复为 1.0 */
    if (bms_supervision_derating_service_is_valid(&axis->resources.derating)) {
        axis->resources.derating.reset(axis->resources.derating.context);
    }
}

const bm_exec_ops_t bm_bms_supervision_exec_ops = {
    bm_bms_supervision_exec_init,
    bm_bms_supervision_exec_start,
    bm_bms_supervision_exec_safe_stop
};

/**
 * @file solar_control.c
 * @brief MPPT 编排与限功率实现
 *
 * 读取电压电流，执行 MPPT 步进并在超限时按功率上限降额。
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
#include "bm/component/solar_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_solar_control_validate_config(const bm_solar_control_config_t *config) {
    if (config == NULL) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_solar_control_reset(bm_solar_control_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_mppt_po_reset(&axis->state.po, axis->config.v_init_v);
    bm_algo_mppt_ic_reset(&axis->state.ic, axis->config.v_init_v);
    axis->state.v_ref_v = axis->config.v_init_v;
    axis->state.last_power_w = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_solar_control_init(bm_solar_control_axis_t *axis) {
    if (axis == NULL ||
        bm_solar_control_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_solar_control_reset(axis);
    return BM_OK;
}

void bm_solar_control_step(bm_solar_control_axis_t *axis) {
    const bm_solar_control_config_t *cfg;
    bm_solar_control_state_t *st;
    float voltage = 0.0f;
    float current = 0.0f;
    float power;
    float v_ref;
    uint32_t status;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_iv == NULL ||
        axis->resources.read_iv(axis->resources.read_iv_user,
                                &voltage, &current) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_SOLAR_CTRL_TEL_STALE;
        st->telemetry.voltage_v = voltage;
        st->telemetry.current_a = current;
        st->telemetry.power_w = 0.0f;
        st->telemetry.v_ref_v = st->v_ref_v;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    power = voltage * current;
    st->last_power_w = power;

    if (cfg->mppt_mode == BM_SOLAR_MPPT_IC) {
        v_ref = bm_algo_mppt_ic_step(&st->ic, &cfg->mppt_ic, voltage, current);
    } else {
        v_ref = bm_algo_mppt_po_step(&st->po, &cfg->mppt_po, voltage, current);
    }

    status = BM_SOLAR_CTRL_TEL_VALID;
    if (cfg->power_limit_w > 0.0f && power > cfg->power_limit_w) {
        float scale = cfg->power_limit_w / power;
        v_ref *= scale;
        status |= BM_SOLAR_CTRL_TEL_LIMITED;
    }

    st->v_ref_v = v_ref;

    if (axis->resources.write_vref != NULL) {
        (void)axis->resources.write_vref(axis->resources.write_vref_user,
                                         v_ref);
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = status;
    st->telemetry.voltage_v = voltage;
    st->telemetry.current_a = current;
    st->telemetry.power_w = power;
    st->telemetry.v_ref_v = v_ref;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

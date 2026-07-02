/**
 * @file bms_estimation.c
 * @brief BMS Pack SOC 估算组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            接入 soc_ekf 模式
 * 2026-06-23       0.3            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/bms_estimation.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <math.h>
#include <string.h>

int bm_bms_estimation_validate_config(const bm_bms_estimation_config_t *config) {
    if (config == NULL || config->coulomb.nominal_capacity_ah <= 0.0f ||
        config->dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->mode == BM_BMS_EST_MODE_EKF) {
        if (config->soc_ekf.nominal_capacity_ah <= 0.0f ||
            config->ocv_slope_v_per_soc <= 0.0f) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

void bm_bms_estimation_reset(bm_bms_estimation_axis_t *axis, float soc_init) {
    if (axis == NULL) {
        return;
    }

    bm_algo_coulomb_reset(&axis->state.coulomb, soc_init);
    bm_algo_soc_ekf_reset(&axis->state.soc_ekf, soc_init);
    axis->state.soc_fused = soc_init;
    axis->state.resting_elapsed_s = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    axis->state.telemetry.soc = soc_init;
    axis->state.telemetry.est_mode = axis->config.mode;
}

void bm_bms_estimation_step(bm_bms_estimation_axis_t *axis) {
    const bm_bms_estimation_config_t *cfg;
    bm_bms_estimation_state_t *st;
    float current_a = 0.0f;
    float voltage_v = 0.0f;
    float temp_c = 25.0f;
    float soc_coulomb;
    float soc_ocv = 0.0f;
    float effective_cap_ah;
    float ocv_comp_v;
    float ocv_from_soc;
    bm_algo_coulomb_config_t coulomb_cfg;
    bm_algo_soc_ekf_config_t ekf_cfg;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.read_sample != NULL) {
        if (axis->resources.read_sample(axis->resources.read_sample_user,
                                      &current_a, &voltage_v, &temp_c) != 0) {
            st->step_count++;
            st->telemetry.sequence = st->step_count;
            st->telemetry.status = BM_BMS_EST_TEL_STALE;
            st->telemetry.est_mode = cfg->mode;
            BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
            return;
        }
    }

    effective_cap_ah = bm_algo_battery_temp_capacity_ah(
        cfg->coulomb.nominal_capacity_ah, temp_c, &cfg->temp);
    coulomb_cfg = cfg->coulomb;
    coulomb_cfg.nominal_capacity_ah = effective_cap_ah;

    soc_coulomb = bm_algo_coulomb_step(&st->coulomb, &coulomb_cfg,
                                       current_a, cfg->dt_s);

    if (fabsf(current_a) <= cfg->resting_current_a) {
        st->resting_elapsed_s += cfg->dt_s;
    } else {
        st->resting_elapsed_s = 0.0f;
    }

    if (cfg->mode == BM_BMS_EST_MODE_EKF) {
        ekf_cfg = cfg->soc_ekf;
        ekf_cfg.nominal_capacity_ah = effective_cap_ah;
        ekf_cfg.ocv_slope_v_per_soc = cfg->ocv_slope_v_per_soc;

        bm_algo_soc_ekf_predict(&st->soc_ekf, &ekf_cfg, current_a, cfg->dt_s);

        if (cfg->ocv_table != NULL) {
            ocv_from_soc = bm_algo_ocv_lookup_soc(cfg->ocv_table, st->soc_ekf.soc);
            ocv_comp_v = bm_algo_battery_temp_compensate_ocv(
                voltage_v, temp_c, &cfg->temp);
            bm_algo_soc_ekf_update_voltage(&st->soc_ekf, &ekf_cfg,
                                           ocv_comp_v, ocv_from_soc);
        }
        /*
         * 无 OCV 表时跳过量测更新，仅保留预测步（P2-6）。此前的 else 分支以
         * ocv_from_soc = ocv_comp_v 自比，量测新息恒为 0，更新步空转（无害但
         * 浪费算力且误导阅读）。缺 SOC-OCV 映射时 EKF 无从校正，正确做法是
         * 只做预测、不做更新。
         */

        st->soc_fused = st->soc_ekf.soc;
    } else {
        if (cfg->ocv_table != NULL &&
            st->resting_elapsed_s >= cfg->resting_time_s) {
            ocv_comp_v = bm_algo_battery_temp_compensate_ocv(
                voltage_v, temp_c, &cfg->temp);
            soc_ocv = bm_algo_ocv_lookup_soc(cfg->ocv_table, ocv_comp_v);
            st->soc_fused = bm_algo_soc_fusion_step(soc_coulomb, soc_ocv,
                                                    &cfg->fusion);
        } else {
            st->soc_fused = soc_coulomb;
        }
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.status = BM_BMS_EST_TEL_VALID;
    if (cfg->mode == BM_BMS_EST_MODE_EKF) {
        st->telemetry.status |= BM_BMS_EST_TEL_EKF;
    }
    st->telemetry.soc = st->soc_fused;
    st->telemetry.pack_voltage_v = voltage_v;
    st->telemetry.pack_current_a = current_a;
    st->telemetry.temp_c = temp_c;
    st->telemetry.est_mode = cfg->mode;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &st->telemetry);
}

/**
 * @brief exec 封装：执行一步 SOC 估算（供调度框架调用）
 *
 * 通过 instance->state 取得 bm_bms_estimation_axis_t 指针后调用
 * bm_bms_estimation_step()。
 *
 * @param instance exec 实例指针，instance->state 须为 bm_bms_estimation_axis_t*
 */
void bm_bms_estimation_exec_step(const bm_exec_t *instance) {
    bm_bms_estimation_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_bms_estimation_axis_t *)instance->state;
    bm_bms_estimation_step(axis);
}

/**
 * @brief exec 生命周期：初始化（校验配置并以 soc_init 复位）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_bms_estimation_exec_init(const bm_exec_t *instance) {
    bm_bms_estimation_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_bms_estimation_axis_t *)instance->state;
    if (bm_bms_estimation_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_bms_estimation_reset(axis, axis->config.soc_init);
    return BM_OK;
}

/**
 * @brief exec 生命周期：启动（当前无额外操作，保留扩展点）
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_bms_estimation_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec 生命周期：安全停机（当前无额外操作）
 *
 * SOC 估算为纯读取组件，停机不需要写入输出，故本函数为空桩。
 *
 * @param instance exec 实例指针
 */
void bm_bms_estimation_exec_safe_stop(const bm_exec_t *instance) {
    (void)instance;
}

/**
 * @brief bms_estimation 标准 exec ops 表
 *
 * 将此指针赋给 bm_exec_t::ops，即可将 bms_estimation 实例
 * 接入调度框架的生命周期管理。
 */
const bm_exec_ops_t bm_bms_estimation_exec_ops = {
    bm_bms_estimation_exec_init,
    bm_bms_estimation_exec_start,
    bm_bms_estimation_exec_safe_stop
};

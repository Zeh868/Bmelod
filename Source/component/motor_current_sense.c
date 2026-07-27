/**
 * @file motor_current_sense.c
 * @brief 2/3 分流电流重构组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.6
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            PWM 扇区采样窗口判定
 * 2026-06-23       0.3            zeh            validate_config 字段校验；公共 API Doxygen；SPDX
 * 2026-07-09       0.4            zeh            缺口 16：3-shunt 分支此前只看
 *                                                resources.adc 是否非空决定要不要读真实
 *                                                ADC 的 ic，未考虑 ia/ib 已走仿真注入路径，
 *                                                导致真实 ic 与仿真 ia/ib 混用破坏 KCL；
 *                                                改为用 use_sim 统一判定
 * 2026-07-27       0.5            zeh            补齐遥测发布能力与 bm_exec_ops_t 调度封装
 * 2026-07-27       0.6            zeh            bm_motor_current_sense_step 返回类型改为 void，
 *                                                错误通过 state.sample_valid/valid 表达并仍发布遥测
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/motor_current_sense.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <string.h>

/**
 * @brief 将 ADC 原始值转换为相电流并扣除零点偏置（静态辅助）
 *
 * 本组件相比 motor_foc_sensorless/sensored 多一个 offset 扣除步骤
 * （标定后的零点漂移补偿），故不能直接复用公共 helper 的返回值语义；
 * 但换算公式的公共部分（(raw-中点)/scale）已委托
 * bm_component_adc_to_current()，避免与另外两处组件重复该公式实现。
 *
 * @param scale  电流标定系数
 * @param raw    ADC 原始计数
 * @param offset 零点偏置（安培），从换算结果中扣除
 * @return 扣除偏置后的电流值（安培）
 */
static float adc_to_current(float scale, uint16_t raw, float offset) {
    return bm_component_adc_to_current(scale, raw) - offset;
}

/**
 * @brief 读取两相 ADC 并换算为电流
 *
 * @param axis 轴实例
 * @param ia   A 相电流输出
 * @param ib   B 相电流输出
 * @return BM_OK 成功；BM_ERR_INVALID 资源配置非法；BM_ERR_IO HAL 读取失败
 */
static int read_adc_pair(const bm_motor_current_sense_axis_t *axis,
                         float *ia,
                         float *ib) {
    const bm_motor_current_sense_resources_t *res = &axis->resources;
    uint16_t raw_ia = 0u;
    uint16_t raw_ib = 0u;

    if (res->adc == NULL || res->adc_scale <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (bm_hal_adc_read_injected(res->adc, res->rank_ia, &raw_ia) != BM_OK) {
        return BM_ERR_IO;
    }
    if (bm_hal_adc_read_injected(res->adc, res->rank_ib, &raw_ib) != BM_OK) {
        return BM_ERR_IO;
    }
    *ia = adc_to_current(res->adc_scale, raw_ia, axis->config.offset_a);
    *ib = adc_to_current(res->adc_scale, raw_ib, axis->config.offset_a);
    return BM_OK;
}

int bm_motor_current_sense_validate_config(
    const bm_motor_current_sense_config_t *config) {
    if (config == NULL) {
        return BM_ERR_INVALID;
    }
    /* topology 枚举合法性 */
    if (config->topology != BM_MOTOR_CS_2SHUNT &&
        config->topology != BM_MOTOR_CS_3SHUNT) {
        return BM_ERR_INVALID;
    }
    /* ADC 触发相位：[0, 360) */
    if (config->adc_phase_deg < 0.0f || config->adc_phase_deg >= 360.0f) {
        return BM_ERR_INVALID;
    }
    /* 采样窗口半宽：0 表示禁用，非零时须 (0, 180) */
    if (config->sample_window_deg < 0.0f ||
        config->sample_window_deg >= 180.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_motor_current_sense_reset(bm_motor_current_sense_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    memset(&axis->state.abc, 0, sizeof(axis->state.abc));
    memset(&axis->state.alphabeta, 0, sizeof(axis->state.alphabeta));
    axis->state.valid = 0;
    axis->state.sample_valid = 0;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_motor_current_sense_init(bm_motor_current_sense_axis_t *axis) {
    const bm_motor_current_sense_resources_t *res;

    if (axis == NULL ||
        bm_motor_current_sense_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    res = &axis->resources;
    if (res->sim_fb.ia_a == NULL && res->sim_fb.ib_a == NULL) {
        if (res->adc == NULL || res->adc_scale <= 0.0f) {
            return BM_ERR_INVALID;
        }
    }
    bm_motor_current_sense_reset(axis);
    return BM_OK;
}

void bm_motor_current_sense_step(bm_motor_current_sense_axis_t *axis) {
    const bm_motor_current_sense_resources_t *res;
    bm_algo_abc_t *abc;
    float ia = 0.0f;
    float ib = 0.0f;
    float ic = 0.0f;

    if (axis == NULL) {
        return;
    }

    res = &axis->resources;
    abc = &axis->state.abc;

    axis->state.sample_valid = 1;
    if (axis->config.sample_window_deg > 0.0f) {
        axis->state.sample_valid = bm_algo_pwm_sample_window_valid(
            axis->config.pwm_sector,
            axis->config.adc_phase_deg,
            axis->config.sample_window_deg);
        if (!axis->state.sample_valid) {
            axis->state.valid = 0;
            goto publish;
        }
    }

    /* 缺口 16：use_sim 记录本拍 ia/ib 是否来自仿真注入。此前 3-shunt 分支
     * 只看 res->adc 是否非空来决定要不要读真实 ADC 的 ic，与 ia/ib 是否走
     * 仿真路径无关——当 sim_fb.ia_a/ib_a 已注入但 resources.adc 仍挂着真实
     * 硬件（如 HIL）时，会把真实 ADC 采到的 ic 和仿真 ia/ib 混在一起，
     * 破坏 ia+ib+ic=0 的物理约束。 */
    {
        int use_sim = (res->sim_fb.ia_a != NULL && res->sim_fb.ib_a != NULL);

        if (use_sim) {
            ia = *res->sim_fb.ia_a;
            ib = *res->sim_fb.ib_a;
            if (res->sim_fb.ic_a != NULL) {
                ic = *res->sim_fb.ic_a;
            } else {
                ic = -(ia + ib);
            }
        } else if (read_adc_pair(axis, &ia, &ib) != BM_OK) {
            axis->state.sample_valid = 0;
            axis->state.valid = 0;
            goto publish;
        }

        if (axis->config.topology == BM_MOTOR_CS_2SHUNT) {
            bm_algo_current_from_2shunt(ia, ib, abc);
            bm_algo_clarke_2shunt(ia, ib, &axis->state.alphabeta);
        } else {
            if (!use_sim && res->adc != NULL && res->sim_fb.ic_a == NULL) {
                uint16_t raw_ic = 0u;
                if (bm_hal_adc_read_injected(res->adc, res->rank_ic,
                                             &raw_ic) != BM_OK) {
                    ic = -(ia + ib);
                } else {
                    ic = adc_to_current(res->adc_scale, raw_ic, axis->config.offset_a);
                }
            } else if (!use_sim && res->sim_fb.ic_a == NULL) {
                ic = -(ia + ib);
            }
            abc->ia = ia;
            abc->ib = ib;
            abc->ic = ic;
            bm_algo_clarke(abc, &axis->state.alphabeta);
        }
    }

    axis->state.valid = 1;

publish:
    axis->state.telemetry.sequence++;
    axis->state.telemetry.ia_a = abc->ia;
    axis->state.telemetry.ib_a = abc->ib;
    axis->state.telemetry.ic_a = abc->ic;
    axis->state.telemetry.alpha_a = axis->state.alphabeta.i_alpha;
    axis->state.telemetry.beta_a = axis->state.alphabeta.i_beta;
    axis->state.telemetry.sample_valid = axis->state.sample_valid;

    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &axis->state.telemetry);
}

/* ---------- exec_ops 封装 ---------- */

/**
 * @brief exec 周期步函数：转发至 bm_motor_current_sense_step
 *
 * @param instance bm_exec 实例；instance->state 须指向
 *                 bm_motor_current_sense_axis_t
 */
void bm_motor_current_sense_exec_step(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_motor_current_sense_step(
            (bm_motor_current_sense_axis_t *)instance->state);
    }
}

/**
 * @brief exec 生命周期：初始化
 *
 * 校验配置合法性并复位状态。
 *
 * @param instance bm_exec 实例
 * @return BM_OK 成功；BM_ERR_INVALID 参数或配置非法
 */
int bm_motor_current_sense_exec_init(const bm_exec_t *instance) {
    bm_motor_current_sense_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_motor_current_sense_axis_t *)instance->state;
    if (bm_motor_current_sense_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_motor_current_sense_reset(axis);
    return BM_OK;
}

/**
 * @brief exec 生命周期：启动
 *
 * 当前无额外启动动作，始终返回 BM_OK。
 *
 * @param instance bm_exec 实例（未使用）
 * @return BM_OK
 */
int bm_motor_current_sense_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

/**
 * @brief exec 生命周期：安全停止
 *
 * 调用 bm_motor_current_sense_reset 清零电流状态与遥测。
 *
 * @param instance bm_exec 实例；instance->state 须指向
 *                 bm_motor_current_sense_axis_t
 */
void bm_motor_current_sense_exec_safe_stop(const bm_exec_t *instance) {
    if (instance != NULL && instance->state != NULL) {
        bm_motor_current_sense_reset(
            (bm_motor_current_sense_axis_t *)instance->state);
    }
}

/** @brief motor_current_sense 标准 exec 生命周期操作表 */
const bm_exec_ops_t bm_motor_current_sense_exec_ops = {
    bm_motor_current_sense_exec_init,
    bm_motor_current_sense_exec_start,
    bm_motor_current_sense_exec_safe_stop
};

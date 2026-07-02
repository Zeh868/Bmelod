/**
 * @file motor_current_sense.c
 * @brief 2/3 分流电流重构组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            PWM 扇区采样窗口判定
 * 2026-06-23       0.3            zeh            validate_config 字段校验；公共 API Doxygen；SPDX
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

static int read_adc_pair(const bm_motor_current_sense_axis_t *axis,
                         float *ia,
                         float *ib) {
    const bm_motor_current_sense_resources_t *res = &axis->resources;
    uint16_t raw_ia = 0u;
    uint16_t raw_ib = 0u;

    if (res->adc == NULL || res->adc_scale <= 0.0f) {
        return -1;
    }
    if (bm_hal_adc_read_injected(res->adc, res->rank_ia, &raw_ia) != BM_OK) {
        return -1;
    }
    if (bm_hal_adc_read_injected(res->adc, res->rank_ib, &raw_ib) != BM_OK) {
        return -1;
    }
    *ia = adc_to_current(res->adc_scale, raw_ia, axis->config.offset_a);
    *ib = adc_to_current(res->adc_scale, raw_ib, axis->config.offset_a);
    return 0;
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

int bm_motor_current_sense_step(bm_motor_current_sense_axis_t *axis) {
    const bm_motor_current_sense_resources_t *res;
    bm_algo_abc_t *abc;
    float ia = 0.0f;
    float ib = 0.0f;
    float ic = 0.0f;

    if (axis == NULL) {
        return BM_ERR_INVALID;
    }

    axis->state.sample_valid = 1;
    if (axis->config.sample_window_deg > 0.0f) {
        axis->state.sample_valid = bm_algo_pwm_sample_window_valid(
            axis->config.pwm_sector,
            axis->config.adc_phase_deg,
            axis->config.sample_window_deg);
        if (!axis->state.sample_valid) {
            axis->state.valid = 0;
            return BM_ERR_INVALID;
        }
    }

    res = &axis->resources;
    abc = &axis->state.abc;

    if (res->sim_fb.ia_a != NULL && res->sim_fb.ib_a != NULL) {
        ia = *res->sim_fb.ia_a;
        ib = *res->sim_fb.ib_a;
        if (res->sim_fb.ic_a != NULL) {
            ic = *res->sim_fb.ic_a;
        } else {
            ic = -(ia + ib);
        }
    } else if (read_adc_pair(axis, &ia, &ib) != 0) {
        axis->state.valid = 0;
        return BM_ERR_INVALID;
    }

    if (axis->config.topology == BM_MOTOR_CS_2SHUNT) {
        bm_algo_current_from_2shunt(ia, ib, abc);
        bm_algo_clarke_2shunt(ia, ib, &axis->state.alphabeta);
    } else {
        if (res->adc != NULL && res->sim_fb.ic_a == NULL) {
            uint16_t raw_ic = 0u;
            if (bm_hal_adc_read_injected(res->adc, res->rank_ic,
                                         &raw_ic) != BM_OK) {
                ic = -(ia + ib);
            } else {
                ic = adc_to_current(res->adc_scale, raw_ic, axis->config.offset_a);
            }
        } else if (res->sim_fb.ic_a == NULL) {
            ic = -(ia + ib);
        }
        abc->ia = ia;
        abc->ib = ib;
        abc->ic = ic;
        bm_algo_clarke(abc, &axis->state.alphabeta);
    }

    axis->state.valid = 1;
    return BM_OK;
}

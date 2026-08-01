/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_adc_sim.h
 * @brief 原生仿真 ADC 实例与测试辅助接口
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */

#ifndef BM_HAL_ADC_SIM_H
#define BM_HAL_ADC_SIM_H

#include "bm_hal_adc.h"

/** 仿真 ADC 实例 0 / 1 */
extern const bm_hal_adc_t BM_HAL_ADC_SIM0;
extern const bm_hal_adc_t BM_HAL_ADC_SIM1;

/**
 * @brief 设置 ADC 注入序列排名的仿真采样值。
 * @param adc ADC 设备实例。
 * @param rank ADC 注入序列排名。
 * @param value 待写入的数值。
 */
void bm_hal_adc_sim_set_rank(const bm_hal_adc_t *adc, uint32_t rank,
                             uint16_t value);
/**
 * @brief 在仿真触发路径中执行 bm_hal_adc_sim_fire_complete 回调或状态更新。 手动触发转换完成回调 */
void bm_hal_adc_sim_fire_complete(const bm_hal_adc_t *adc);

#endif /* BM_HAL_ADC_SIM_H */

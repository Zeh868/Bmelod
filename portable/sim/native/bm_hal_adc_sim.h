/**
 * @file bm_hal_adc_sim.h
 * @brief 原生仿真 ADC 实例与测试辅助接口
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
 */

#ifndef BM_HAL_ADC_SIM_H
#define BM_HAL_ADC_SIM_H

#include "bm_hal_adc.h"

/** 仿真 ADC 实例 0 / 1 */
extern const bm_hal_adc_t BM_HAL_ADC_SIM0;
extern const bm_hal_adc_t BM_HAL_ADC_SIM1;

/** 设置 rank 采样值（单元测试/仿真注入） */
void bm_hal_adc_sim_set_rank(const bm_hal_adc_t *adc, uint32_t rank,
                             uint16_t value);
/** 手动触发转换完成回调 */
void bm_hal_adc_sim_fire_complete(const bm_hal_adc_t *adc);

#endif /* BM_HAL_ADC_SIM_H */

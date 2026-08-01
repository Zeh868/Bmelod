/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_adc_stm32g4.h
 * @brief STM32G474xB 相电流 ADC（ADC1 injected）实例声明
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#ifndef BM_VENDOR_ADC_STM32G4_H
#define BM_VENDOR_ADC_STM32G4_H

#include "bm_hal_adc.h"

/** @brief M0 电机相电流 ADC 实例（ADC1 注入组 ia/ib 双 rank）。 */
extern const bm_hal_adc_t bm_hal_adc_m0;

#endif /* BM_VENDOR_ADC_STM32G4_H */

/**
 * @file bm_vendor_adc_esp32_idf.h
 * @brief ESP32-WROOM-32E 板级 ADC 实例声明
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 M0/M1 双电机 ADC 实例
 *
 */
#ifndef BM_VENDOR_ADC_ESP32_IDF_H
#define BM_VENDOR_ADC_ESP32_IDF_H

#include "bm_hal_adc.h"

/** @brief M0 电机 ADC 实例。 */
extern const bm_hal_adc_t bm_hal_adc_m0;
/** @brief M1 电机 ADC 实例。 */
extern const bm_hal_adc_t bm_hal_adc_m1;

#endif /* BM_VENDOR_ADC_ESP32_IDF_H */

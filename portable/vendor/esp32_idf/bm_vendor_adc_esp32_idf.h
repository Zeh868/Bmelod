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
#include <stdint.h>

/** @brief M0 电机 ADC 实例。 */
extern const bm_hal_adc_t bm_hal_adc_m0;
/** @brief M1 电机 ADC 实例。 */
extern const bm_hal_adc_t bm_hal_adc_m1;

/**
 * @brief 诊断埋点：读取并清零 ADC ISR 采样耗时统计（主循环每秒调用）。
 *
 * 返回自上次调用以来（约 1s 内）ADC isr_sample 的 cycle 最大值、
 * 平均值（cycles_sum/cnt），以及轮询 wait 最大次数。
 * 调用后内部统计清零，供主循环每秒打印一次。
 * 仅在主循环（SRT）调用，绝不在 ISR 内调用。
 *
 * @note 诊断埋点，配合 HOVER_FOC_ENERGIZE=0 使用；摘除方法同 .c 内注释。
 *
 * @param[out] cycles_max  本窗口内单次最大耗时（CPU 周期数，除以 CPU_Hz 得秒）。
 * @param[out] cycles_avg  本窗口内单次平均耗时（CPU 周期数）；cnt=0 时输出 0。
 * @param[out] wait_max    本窗口内轮询 wait 最大次数。
 */
void bm_vendor_adc_esp32_idf_diag_read_clear(uint32_t *cycles_max,
                                             uint32_t *cycles_avg,
                                             uint32_t *wait_max);

#endif /* BM_VENDOR_ADC_ESP32_IDF_H */

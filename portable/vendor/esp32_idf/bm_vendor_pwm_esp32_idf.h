/**
 * @file bm_vendor_pwm_esp32_idf.h
 * @brief ESP32-WROOM-32E 板级 PWM 实例声明（Phase 2：MCPWM 硬件驱动）
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 M0/M1 双电机 PWM 实例
 * 2026-06-19       2.0            zeh            Phase 2：改为 MCPWM 硬件驱动接口
 *
 */
#ifndef BM_VENDOR_PWM_ESP32_IDF_H
#define BM_VENDOR_PWM_ESP32_IDF_H

#include "bm_hal_pwm.h"
#include <stdint.h>

/** @brief M0 电机 PWM 实例（MCPWM unit0）。 */
extern const bm_hal_pwm_t bm_hal_pwm_m0;
/** @brief M1 电机 PWM 实例（MCPWM unit1）。 */
extern const bm_hal_pwm_t bm_hal_pwm_m1;

/**
 * @brief 将 ADC 完成回调绑定到指定电机的 MCPWM ISR。
 *
 * 由 ADC 驱动在初始化或回调注册时调用；MCPWM TEZ ISR 在采样后触发该回调。
 *
 * @param motor_id 电机编号（0/1）。
 * @param binding  HRT 绑定；NULL 表示清除。
 */
void bm_vendor_pwm_esp32_idf_bind_adc_complete(uint32_t motor_id,
                                                const bm_hal_hrt_binding_t *binding);

/**
 * @brief 诊断埋点：读取并清零 PWM ISR 有效负载段耗时统计（主循环每秒调用）。
 *
 * 返回自上次调用以来（约 1s 内）ISR 有效负载段的 cycle 最大值、均值，
 * 以及 ISR 触发总拍数（约等于实际 current_step 执行率 Hz）。
 * 调用后内部统计清零。仅在主循环（SRT）调用，绝不在 ISR 内调用。
 *
 * @note 诊断埋点（DIAG_ISR_BEGIN/END 标记块），ENERGIZE=1 后按需摘除。
 *
 * @param[out] cycles_max  本窗口内单次最大耗时（CPU 周期数）。
 * @param[out] cycles_avg  本窗口内单次平均耗时（CPU 周期数）；cnt=0 时输出 0。
 * @param[out] isr_cnt     本窗口内 ISR 触发总拍数（约等于 current_step 实际率 Hz）。
 */
void bm_vendor_pwm_esp32_idf_diag_read_clear(uint32_t *cycles_max,
                                             uint32_t *cycles_avg,
                                             uint32_t *isr_cnt);

#endif /* BM_VENDOR_PWM_ESP32_IDF_H */

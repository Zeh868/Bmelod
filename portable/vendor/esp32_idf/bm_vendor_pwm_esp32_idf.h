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

#endif /* BM_VENDOR_PWM_ESP32_IDF_H */

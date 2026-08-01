/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_devices_esp32_idf.h
 * @brief ESP32（ESP-IDF）后端实例出口（devices 聚合头 + default 别名）
 * @maturity E1
 *
 * 经 `include/hal/bm_hal_devices.h` 由 pack 宏
 * `BM_HAL_DEVICES_HEADER="bm_hal_devices_esp32_idf.h"` 引入；
 * 聚合 ESP32 后端全部 `bm_hal_*` 实例声明（include 既有实例头，
 * 不重复 extern 声明），并提供 `bm_<class>_default` 首选实例别名。
 *
 * ESP32 侧 GPIO/HRTimer 实例缺口（P3）未补，故不定义对应别名
 * （应用误用为编译期报错，fail-closed）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh            新增（P1 跨后端实例出口）
 *
 * 2026-08-01       1.0            Codex            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_DEVICES_ESP32_IDF_H
#define BM_HAL_DEVICES_ESP32_IDF_H

#include "bm_vendor_pwm_esp32_idf.h"
#include "bm_vendor_adc_esp32_idf.h"
#include "bm_vendor_encoder_esp32_idf.h"
#include "bm_hal_i2c_esp32_idf.h"

/** @brief 首选 PWM 实例。 */
#define bm_pwm_default      bm_hal_pwm_m0
/** @brief 首选 ADC 实例。 */
#define bm_adc_default      bm_hal_adc_m0
/** @brief 首选编码器实例（AS5600，挂 I2C1）。 */
#define bm_encoder_default  bm_hal_encoder_m0
/** @brief 首选 I2C 总线实例（板级传感器所在 I2C1）。 */
#define bm_i2c_default      bm_hal_i2c_1

#endif /* BM_HAL_DEVICES_ESP32_IDF_H */

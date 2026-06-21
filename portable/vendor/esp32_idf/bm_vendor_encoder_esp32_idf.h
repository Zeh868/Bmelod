/**
 * @file bm_vendor_encoder_esp32_idf.h
 * @brief ESP32-WROOM-32E 板级编码器实例声明
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 M0/M1 双电机编码器实例
 * 2026-06-21       1.1            zeh           增加读钩子声明，允许上层接管为硬件 I2C
 *
 */
#ifndef BM_VENDOR_ENCODER_ESP32_IDF_H
#define BM_VENDOR_ENCODER_ESP32_IDF_H

#include "bm_hal_encoder.h"

/** @brief M0 电机编码器实例。 */
extern const bm_hal_encoder_t bm_hal_encoder_m0;
/** @brief M1 电机编码器实例。 */
extern const bm_hal_encoder_t bm_hal_encoder_m1;

/**
 * @brief 上层可接管的编码器读钩子。
 *
 * 设置为非 NULL 后，bm_hal_encoder_read 将转发到该函数，用于硬件 I2C 接管。
 */
extern int (*bm_vendor_encoder_read_hook)(const struct bm_hal_encoder *dev,
                                          int32_t *value);

#endif /* BM_VENDOR_ENCODER_ESP32_IDF_H */

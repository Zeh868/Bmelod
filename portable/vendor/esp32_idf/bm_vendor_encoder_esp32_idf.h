/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_encoder_esp32_idf.h
 * @brief ESP32-WROOM-32E 板级编码器实例声明
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 M0/M1 双电机编码器实例
 * 2026-06-21       1.1            zeh           增加读钩子声明，允许上层接管为硬件 I2C
 * 2026-08-01       1.2            zeh           删除死声明 bm_vendor_encoder_read_hook
 *                                                （全仓无定义无调用）；实现迁往
 *                                                bm_hal_i2c 总线设备（vendor 内部契约
 *                                                变更，config 以 bus 指针替代 i2c_port）
 *
 */
#ifndef BM_VENDOR_ENCODER_ESP32_IDF_H
#define BM_VENDOR_ENCODER_ESP32_IDF_H

#include "bm_hal_encoder.h"

/** @brief M0 电机编码器实例。 */
extern const bm_hal_encoder_t bm_hal_encoder_m0;
/** @brief M1 电机编码器实例。 */
extern const bm_hal_encoder_t bm_hal_encoder_m1;

#endif /* BM_VENDOR_ENCODER_ESP32_IDF_H */

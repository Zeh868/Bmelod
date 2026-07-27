/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_encoder_stm32g4.h
 * @brief STM32G474xB 增量编码器（TIM3 正交模式）实例声明
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 *
 */
#ifndef BM_VENDOR_ENCODER_STM32G4_H
#define BM_VENDOR_ENCODER_STM32G4_H

#include "bm_hal_encoder.h"

/** @brief M0 电机增量编码器实例（TIM3 正交编码器模式）。 */
extern const bm_hal_encoder_t bm_hal_encoder_m0;

#endif /* BM_VENDOR_ENCODER_STM32G4_H */

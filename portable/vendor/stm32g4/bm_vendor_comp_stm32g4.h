/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_comp_stm32g4.h
 * @brief STM32G474xB 过流比较器（COMP1）实例声明
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
#ifndef BM_VENDOR_COMP_STM32G4_H
#define BM_VENDOR_COMP_STM32G4_H

#include "bm_hal_comp.h"

/** @brief M0 电机过流比较器实例（COMP1，输出内部直连 TIM1_BKIN）。 */
extern const bm_hal_comp_t bm_hal_comp_m0;

#endif /* BM_VENDOR_COMP_STM32G4_H */

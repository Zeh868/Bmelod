/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_hrtimer_stm32g4.h
 * @brief STM32G4 高精度 Timer 后端配置与实例声明
 * @maturity E1
 *
 * App 通过 `bm_hrtimer_stm32g4_config_t` 指定实际 TIM/通道/IRQ；
 * Bmelod 不固定 TIM 编号。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 高精度 Timer 后端
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#ifndef BM_VENDOR_HRTIMER_STM32G4_H
#define BM_VENDOR_HRTIMER_STM32G4_H

#include "hal/bm_hal_hrtimer.h"

#include <stdint.h>

#include "stm32g4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STM32G4 高精度 Timer 平台配置。
 *
 * App 静态分配并填写；init 时由后端校验 TIM/通道/IRQ 合法性。
 */
typedef struct {
    TIM_TypeDef *tim;        /**< TIM 寄存器基址（如 TIM2/TIM3/TIM4/...） */
    uint32_t     channel;    /**< LL_TIM_CHANNEL_CHx */
    IRQn_Type    irqn;       /**< 对应中断向量（如 TIM2_IRQn/TIM3_IRQn） */
    uint32_t     rcc_apb1;   /**< APB1 时钟使能位；为 0 时从 APB2 取 */
    uint32_t     rcc_apb2;   /**< APB2 时钟使能位；为 0 时从 APB1 取 */
    uint32_t     prescaler;  /**< 预分频值（0 表示 1 分频） */
    uint32_t     auto_reload;/**< 自动重装载值（决定最大计数周期） */
    uint32_t     irq_priority;/**< NVIC 优先级 */
} bm_hrtimer_stm32g4_config_t;

/** @brief 默认 hrtimer0 实例（TIM2，CH1，APB1）。 */
extern const bm_hal_hrtimer_t bm_stm32g4_hrtimer0;

/** @brief 默认 hrtimer1 实例（TIM3，CH1，APB1）。 */
extern const bm_hal_hrtimer_t bm_stm32g4_hrtimer1;

#ifdef __cplusplus
}
#endif

#endif /* BM_VENDOR_HRTIMER_STM32G4_H */

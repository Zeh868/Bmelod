/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_critical.h
 * @brief 临界区 HAL 接口
 *
 * 封装平台关中断/恢复中断，可选支持按优先级阈值屏蔽。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_HAL_CRITICAL_H
#define BM_HAL_CRITICAL_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifndef BM_HAL_HAS_PRIORITY_MASK
#define BM_HAL_HAS_PRIORITY_MASK 0
#endif

/**
 * @brief 关中断并进入临界区
 *
 * @return 先前 IRQ 状态，供 bm_hal_critical_exit 恢复
 */
bm_irq_state_t bm_hal_critical_enter(void);

/**
 * @brief 退出临界区并恢复 IRQ 状态
 *
 * @param state bm_hal_critical_enter 返回的状态值
 */
void bm_hal_critical_exit(bm_irq_state_t state);

/**
 * @brief 当前是否处于 ISR/中断上下文
 */
int bm_hal_in_isr(void);

#if BM_HAL_HAS_PRIORITY_MASK
/**
 * @brief 屏蔽优先级低于阈值的中断并进入临界区
 *
 * @param threshold 优先级阈值（低于该值的中断被屏蔽）
 * @return 先前 IRQ 状态，供 bm_hal_critical_exit_below 恢复
 */
bm_irq_state_t bm_hal_critical_enter_below(uint8_t threshold);

/**
 * @brief 退出临界区并恢复 IRQ 状态
 *
 * @param previous_state bm_hal_critical_enter_below 返回的状态值
 */
void bm_hal_critical_exit_below(bm_irq_state_t previous_state);
#endif

#endif /* BM_HAL_CRITICAL_H */

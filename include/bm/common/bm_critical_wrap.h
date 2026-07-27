/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_critical_wrap.h
 * @brief 临界区进入/退出宏封装
 *
 * 根据 BM_CONFIG_ENABLE_PRIORITY_MASK 选择全局关中断或优先级阈值屏蔽。
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
#ifndef BM_CRITICAL_WRAP_H
#define BM_CRITICAL_WRAP_H

#include "bm/common/bm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 进入临界区并保存先前 IRQ 状态
 * @return 先前 IRQ 状态
 */
extern bm_irq_state_t bm_critical_enter(void);

/**
 * @brief 退出临界区并恢复 IRQ 状态
 * @param state bm_critical_enter 返回的状态值
 */
extern void bm_critical_exit(bm_irq_state_t state);

/**
 * @brief 当前是否处于 ISR/中断上下文
 * @return 非 0 表示在 ISR 中
 */
extern int bm_in_isr(void);

#ifdef __cplusplus
}
#endif

#ifndef BM_CONFIG_ENABLE_PRIORITY_MASK
#define BM_CONFIG_ENABLE_PRIORITY_MASK 0
#endif

#ifndef BM_HAL_HAS_PRIORITY_MASK
#define BM_HAL_HAS_PRIORITY_MASK 0
#endif

#if BM_CONFIG_ENABLE_PRIORITY_MASK
#if !BM_HAL_HAS_PRIORITY_MASK
#error "BM_CONFIG_ENABLE_PRIORITY_MASK 需要 BM_HAL_HAS_PRIORITY_MASK"
#endif
#ifndef BM_CONFIG_HRT_PRIORITY_THRESHOLD
#define BM_CONFIG_HRT_PRIORITY_THRESHOLD 4
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 屏蔽优先级低于阈值的中断并进入临界区
 * @param threshold 优先级阈值（低于该值的中断被屏蔽）
 * @return 先前 IRQ 状态
 */
extern bm_irq_state_t bm_critical_enter_below(uint8_t threshold);

/**
 * @brief 退出按优先级屏蔽的临界区
 * @param previous_state bm_critical_enter_below 返回的状态值
 */
extern void bm_critical_exit_below(bm_irq_state_t previous_state);

#ifdef __cplusplus
}
#endif

/** 屏蔽低于 HRT 阈值的中断并进入临界区 */
#define BM_CRITICAL_ENTER() \
    bm_critical_enter_below((uint8_t)BM_CONFIG_HRT_PRIORITY_THRESHOLD)
#define BM_CRITICAL_EXIT(state) bm_critical_exit_below(state)
#else
/** 全局关中断进入临界区 */
#define BM_CRITICAL_ENTER() bm_critical_enter()
#define BM_CRITICAL_EXIT(state) bm_critical_exit(state)
#endif

#endif /* BM_CRITICAL_WRAP_H */

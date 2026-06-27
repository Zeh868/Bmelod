/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_ops.h
 * @brief 架构层原语契约
 *
 * 各 `portable/arch/<id>/` 实现下列符号；`bm_arch_drv_bundle.c` 将其组装为
 * `bm_drv_critical_api` 与 `bm_drv_memory_api`。不得在此头文件中暴露厂商 SDK 类型。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#ifndef BM_ARCH_OPS_H
#define BM_ARCH_OPS_H

#include "hal/bm_hal_critical.h"

/**
 * @brief 关中断并进入临界区
 *
 * @return 先前 IRQ 状态，供 bm_arch_critical_exit 恢复
 */
bm_irq_state_t bm_arch_critical_enter(void);

/**
 * @brief 退出临界区并恢复 IRQ 状态
 *
 * @param state bm_arch_critical_enter 返回的状态值
 */
void bm_arch_critical_exit(bm_irq_state_t state);

/**
 * @brief 当前是否处于 ISR/中断上下文
 */
int bm_arch_in_isr(void);

#if BM_HAL_HAS_PRIORITY_MASK
/**
 * @brief 屏蔽优先级低于阈值的中断并进入临界区
 *
 * @param threshold 优先级阈值（低于该值的中断被屏蔽）
 * @return 先前 IRQ 状态，供 bm_arch_critical_exit_below 恢复
 */
bm_irq_state_t bm_arch_critical_enter_below(uint8_t threshold);

/**
 * @brief 退出临界区并恢复 IRQ 状态
 *
 * @param previous_state bm_arch_critical_enter_below 返回的状态值
 */
void bm_arch_critical_exit_below(bm_irq_state_t previous_state);
#endif

/**
 * @brief store-release 语义内存屏障
 */
void bm_arch_memory_barrier_release(void);

/**
 * @brief 完全内存屏障（顺序一致性）
 */
void bm_arch_memory_barrier_full(void);

#endif /* BM_ARCH_OPS_H */

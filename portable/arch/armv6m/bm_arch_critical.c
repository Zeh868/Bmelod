/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief ARMv6-M / ARMv7-M 临界区实现（primask，无优先级掩码）
 *
 * Cortex-M0/M0+/M3 路径：全关中断；若编译时启用 BM_HAL_HAS_PRIORITY_MASK
 * 则 enter_below 退化为全关中断（与 armv7em 区分）。
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
#include "port/bm_arch_ops.h"
#include "arm/common/bm_arch_arm_primask.h"

bm_irq_state_t bm_arch_critical_enter(void) {
    bm_irq_state_t state = (bm_irq_state_t)bm_arch_arm_read_primask();
    __asm volatile ("cpsid i" ::: "memory");
    return state;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    bm_arch_arm_write_primask((uint32_t)state);
}

int bm_arch_in_isr(void) {
    return bm_arch_arm_in_isr();
}

#if BM_HAL_HAS_PRIORITY_MASK
bm_irq_state_t bm_arch_critical_enter_below(uint8_t threshold) {
    (void)threshold;
    return bm_arch_critical_enter();
}

void bm_arch_critical_exit_below(bm_irq_state_t previous_state) {
    bm_arch_critical_exit(previous_state);
}
#endif

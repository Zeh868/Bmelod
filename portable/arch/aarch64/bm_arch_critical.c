/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief AArch64 临界区实现（DAIF 保存/恢复）
 *
 * 进入临界区时保存 DAIF 并通过 DAIFSet 屏蔽 IRQ/FIQ；退出时完整恢复。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */
#include "port/bm_arch_ops.h"
#include "aarch64/bm_arch_portmacro.h"

bm_irq_state_t bm_arch_critical_enter(void) {
    bm_arch_daif_state_t daif;

    __asm volatile("mrs %0, daif" : "=r"(daif));
    __asm volatile("msr daifset, #3" ::: "memory");
    return (bm_irq_state_t)daif;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    __asm volatile("msr daif, %0" ::"r"((bm_arch_daif_state_t)state) : "memory");
}

int bm_arch_in_isr(void) {
    /*
     * EL1 baremetal 无统一硬件 ISR 判定；GIC 确认寄存器需平台耦合。
     * 简化实现：始终报告线程上下文（与任务包约定一致）。
     */
    return 0;
}

/**
 * @file bm_arch_critical.c
 * @brief ARMv7-A 临界区实现（CPSR I/F 保存/恢复）
 *
 * 进入临界区时保存 CPSR 并通过 cpsid if 屏蔽 IRQ/FIQ；退出时恢复 cpsr_c。
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
#include "armv7a/bm_arch_portmacro.h"

bm_irq_state_t bm_arch_critical_enter(void) {
    uint32_t cpsr;

    __asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    __asm volatile("cpsid if" ::: "memory");
    return (bm_irq_state_t)cpsr;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    __asm volatile("msr cpsr_c, %0" ::"r"((uint32_t)state) : "memory");
}

int bm_arch_in_isr(void) {
    uint32_t cpsr;
    uint32_t mode;

    __asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    mode = cpsr & 0x1Fu;
    return (mode != 0x10u && mode != 0x1Fu) ? 1 : 0;
}

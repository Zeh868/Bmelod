/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief AArch64 临界区实现（DAIF 保存/恢复）与 ISR 上下文判定
 *
 * 进入临界区时保存 DAIF 并通过 DAIFSet 屏蔽 IRQ/FIQ；退出时完整恢复。
 * ISR 判定采用按核 IRQ 嵌套计数：后端包在 IRQ 分发首尾调用
 * bm_arch_aarch64_irq_enter/exit（见 bm_arch_portmacro.h）。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-30       1.1            zeh            bm_arch_in_isr 由恒 0 改为按核
 *                                               IRQ 嵌套计数真实判定（修复
 *                                               aarch64 上 ISR 防护全部失效）
 *
 */
#include "port/bm_arch_ops.h"
#include "aarch64/bm_arch_portmacro.h"
#include "bm_config.h"

/** 按核 IRQ 嵌套深度；IRQ 入口 PSTATE.I 已置位，同核增减天然串行 */
static volatile uint32_t g_aarch64_irq_depth[BM_CONFIG_CPU_COUNT];

/** @brief 当前核编号（MPIDR_EL1 Aff0），钳到计数数组界内 */
static uint32_t bm_arch_aarch64_cpu_index(void) {
    uint64_t mpidr;
    uint32_t cpu;

    __asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    cpu = (uint32_t)(mpidr & 0xFFu);
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        cpu = BM_CONFIG_CPU_COUNT - 1u;
    }
    return cpu;
}

bm_irq_state_t bm_arch_critical_enter(void) {
    bm_arch_daif_state_t daif;

    __asm volatile("mrs %0, daif" : "=r"(daif));
    __asm volatile("msr daifset, #3" ::: "memory");
    return (bm_irq_state_t)daif;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    __asm volatile("msr daif, %0" ::"r"((bm_arch_daif_state_t)state) : "memory");
}

void bm_arch_aarch64_irq_enter(void) {
    g_aarch64_irq_depth[bm_arch_aarch64_cpu_index()]++;
}

void bm_arch_aarch64_irq_exit(void) {
    volatile uint32_t *depth = &g_aarch64_irq_depth[bm_arch_aarch64_cpu_index()];

    if (*depth > 0u) {
        *depth = *depth - 1u;
    }
}

int bm_arch_in_isr(void) {
    return (g_aarch64_irq_depth[bm_arch_aarch64_cpu_index()] != 0u) ? 1 : 0;
}

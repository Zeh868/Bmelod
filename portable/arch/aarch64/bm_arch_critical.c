/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief AArch64 临界区实现（DAIF 保存/恢复）与 ISR 上下文判定
 *
 * 进入临界区时保存 DAIF 并通过 DAIFSet 屏蔽 IRQ/FIQ；退出时完整恢复。
 * ISR 判定采用按核 IRQ 嵌套计数：后端包在 IRQ 分发首尾调用
 * bm_arch_aarch64_irq_enter/exit（见 bm_arch_portmacro.h）。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-30       1.1            zeh            bm_arch_in_isr 由恒 0 改为按核
 *                                               IRQ 嵌套计数真实判定（修复
 *                                               aarch64 上 ISR 防护全部失效）
 * 2026-07-31       1.2            zeh            按核计数补 cache-line 隔离
 * 2026-08-01       1.2            zeh           补齐 Doxygen 合规元数据
 *
 */
#include "port/bm_arch_ops.h"
#include "aarch64/bm_arch_portmacro.h"
#include "bm_config.h"

/**
 * 按核 IRQ 嵌套深度；IRQ 入口 PSTATE.I 已置位，同核增减天然串行。
 * 补齐到整 cache-line：多核各自的计数落在独立行上，避免伪共享，
 * 也避免缓存非一致场景下整行回写互相覆盖邻核计数。
 */
typedef union {
    volatile uint32_t depth;
    uint8_t cache_line_span[BM_CONFIG_CACHE_LINE];
} bm_arch_irq_depth_t;

static bm_arch_irq_depth_t g_aarch64_irq_depth[BM_CONFIG_CPU_COUNT]
    __attribute__((aligned(BM_CONFIG_CACHE_LINE)));

/**
 * @brief 取当前核（MPIDR_EL1 Aff0）的 IRQ 深度计数槽
 *
 * CPU 编号越界属配置错误（BM_CONFIG_CPU_COUNT 少于实际核数），此处钳到界内，
 * 保证 enter/exit/查询三者落在同一槽上，判定自洽。
 */
static bm_arch_irq_depth_t *bm_arch_aarch64_irq_slot(void) {
    uint64_t mpidr;
    uint32_t cpu;

    __asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    cpu = (uint32_t)(mpidr & 0xFFu);
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        cpu = BM_CONFIG_CPU_COUNT - 1u;
    }
    return &g_aarch64_irq_depth[cpu];
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
    bm_arch_aarch64_irq_slot()->depth++;
}

void bm_arch_aarch64_irq_exit(void) {
    bm_arch_irq_depth_t *slot = bm_arch_aarch64_irq_slot();

    if (slot->depth > 0u) {
        slot->depth--;
    }
}

int bm_arch_in_isr(void) {
    return (bm_arch_aarch64_irq_slot()->depth != 0u) ? 1 : 0;
}

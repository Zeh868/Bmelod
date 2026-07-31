/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_portmacro.h
 * @brief AArch64 架构宏（DAIF / 内存屏障 / 让步）
 *
 * AArch64 临界区快照为 DAIF 寄存器值（64 位语义，经 bm_irq_state_t 截断传递）。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-30
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-30       1.1            zeh            声明 IRQ 嵌套计数钩子
 *                                               （bm_arch_aarch64_irq_enter/exit），
 *                                               支撑真实的 bm_arch_in_isr 判定
 *
 */

#ifndef BM_ARCH_PORTMACRO_H
#define BM_ARCH_PORTMACRO_H

#include <stdint.h>

/** AArch64 DAIF 快照在 arch 层以 64 位保存，对外 API 使用 bm_irq_state_t */
typedef uint64_t bm_arch_daif_state_t;

/** 数据内存屏障 */
#define BM_ARCH_DMB() __asm volatile("dmb sy" ::: "memory")

/** 数据同步屏障 */
#define BM_ARCH_DSB() __asm volatile("dsb sy" ::: "memory")

/** 指令同步屏障 */
#define BM_ARCH_ISB() __asm volatile("isb" ::: "memory")

/** 忙等待让步 */
#define BM_ARCH_YIELD() __asm volatile("yield")

#if defined(__GNUC__) || defined(__clang__)
#define BM_ARCH_ALIGN(n) __attribute__((aligned(n)))
#else
#define BM_ARCH_ALIGN(n)
#endif

/**
 * @brief IRQ 入口嵌套计数 +1（由异常向量分发起点调用）
 *
 * AArch64 EL1 无 CPSR.mode 类硬件字段可判定 ISR 上下文（ARMv7-A 靠 CPSR
 * mode 位），故由后端包在 IRQ 分发首尾成对调用本钩子维护按核计数，
 * bm_arch_in_isr 据此给出真实判定。
 */
void bm_arch_aarch64_irq_enter(void);

/**
 * @brief IRQ 出口嵌套计数 -1（与 bm_arch_aarch64_irq_enter 成对）
 */
void bm_arch_aarch64_irq_exit(void);

#endif /* BM_ARCH_PORTMACRO_H */

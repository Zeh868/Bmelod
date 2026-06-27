/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_portmacro.h
 * @brief AArch64 架构宏（DAIF / 内存屏障 / 让步）
 *
 * AArch64 临界区快照为 DAIF 寄存器值（64 位语义，经 bm_irq_state_t 截断传递）。
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

#endif /* BM_ARCH_PORTMACRO_H */

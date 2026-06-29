/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_portmacro.h
 * @brief Xtensa 架构宏（memw 屏障、waiti 让步）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            Phase 3 正式发布
 *
 */

#ifndef BM_ARCH_PORTMACRO_H
#define BM_ARCH_PORTMACRO_H

/** Xtensa 内存写屏障 */
#define BM_ARCH_DMB() __asm__ volatile ("memw" ::: "memory")

/** 低功耗让步：waiti 0（无可用指令时退化为编译器屏障） */
#if defined(__XTENSA__)
#define BM_ARCH_YIELD() __asm__ volatile ("waiti 0")
#else
#define BM_ARCH_YIELD() __asm__ volatile ("" ::: "memory")
#endif

#if defined(__GNUC__) || defined(__clang__)
#define BM_ARCH_ALIGN(n) __attribute__((aligned(n)))
#else
#define BM_ARCH_ALIGN(n)
#endif

#endif /* BM_ARCH_PORTMACRO_H */

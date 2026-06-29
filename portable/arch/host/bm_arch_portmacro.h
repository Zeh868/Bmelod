/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_portmacro.h
 * @brief 宿主架构宏（PC 单元测试 / native_sim）
 *
 * 无硬件 DMB/WFI；BM_ARCH_DMB 退化为编译器屏障，BM_ARCH_YIELD 为空操作。
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

/** 编译器内存屏障（宿主无硬件 DMB） */
#if defined(_MSC_VER)
#include <intrin.h>
#define BM_ARCH_DMB() _ReadWriteBarrier()
#else
#define BM_ARCH_DMB() __asm volatile ("" ::: "memory")
#endif

/** 宿主让步：空操作 */
#define BM_ARCH_YIELD() ((void)0)

#if defined(_MSC_VER)
#define BM_ARCH_ALIGN(n) __declspec(align(n))
#elif defined(__GNUC__) || defined(__clang__)
#define BM_ARCH_ALIGN(n) __attribute__((aligned(n)))
#else
#define BM_ARCH_ALIGN(n)
#endif

#endif /* BM_ARCH_PORTMACRO_H */

/**
 * @file bm_arch_arm_portmacro.h
 * @brief ARM 架构层共享宏（primask / basepri 系列）
 *
 * 供 armv6m、armv7em、armv8m_main 等目录的 bm_arch_portmacro.h 转发引用。
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

#ifndef BM_ARCH_ARM_PORTMACRO_H
#define BM_ARCH_ARM_PORTMACRO_H

/** 数据内存屏障（与 bm_arch_memory_barrier_release 中 dmb 语义一致） */
#define BM_ARCH_DMB() __asm volatile ("dmb" ::: "memory")

/** 忙等待让步：等待中断（WFI） */
#define BM_ARCH_YIELD() __asm volatile ("wfi")

#if defined(__GNUC__) || defined(__ICCARM__) || defined(__clang__)
#define BM_ARCH_ALIGN(n) __attribute__((aligned(n)))
#else
#define BM_ARCH_ALIGN(n)
#endif

#endif /* BM_ARCH_ARM_PORTMACRO_H */

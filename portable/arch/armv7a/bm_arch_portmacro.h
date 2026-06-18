/**
 * @file bm_arch_portmacro.h
 * @brief ARMv7-A 架构宏（CPSR / 内存屏障 / 让步）
 *
 * Cortex-A 路径：临界区快照为 CPSR 值（经 bm_irq_state_t 传递）。
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

/** 数据内存屏障 */
#define BM_ARCH_DMB() __asm volatile("dmb" ::: "memory")

/** 数据同步屏障 */
#define BM_ARCH_DSB() __asm volatile("dsb" ::: "memory")

/** 指令同步屏障 */
#define BM_ARCH_ISB() __asm volatile("isb" ::: "memory")

/** 忙等待让步：等待事件（WFE） */
#define BM_ARCH_YIELD() __asm volatile("wfe")

#if defined(__GNUC__) || defined(__clang__)
#define BM_ARCH_ALIGN(n) __attribute__((aligned(n)))
#else
#define BM_ARCH_ALIGN(n)
#endif

#endif /* BM_ARCH_PORTMACRO_H */

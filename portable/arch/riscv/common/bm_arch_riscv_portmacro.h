/**
 * @file bm_arch_riscv_portmacro.h
 * @brief RISC-V 架构层共享宏（RV32 / RV64，由 __riscv_xlen 区分宽度）
 *
 * 供 riscv32、riscv64 目录的 bm_arch_portmacro.h 转发引用。
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

#ifndef BM_ARCH_RISCV_PORTMACRO_H
#define BM_ARCH_RISCV_PORTMACRO_H

/** 数据内存屏障 */
#define BM_ARCH_DMB() __asm volatile ("fence rw, rw" ::: "memory")

/** 指令缓存一致性屏障 */
#define BM_ARCH_FENCE_I() __asm volatile ("fence.i" ::: "memory")

/** 忙等待让步：等待中断 */
#define BM_ARCH_YIELD() __asm volatile ("wfi")

#if defined(__GNUC__) || defined(__clang__)
#define BM_ARCH_ALIGN(n) __attribute__((aligned(n)))
#else
#define BM_ARCH_ALIGN(n)
#endif

#endif /* BM_ARCH_RISCV_PORTMACRO_H */

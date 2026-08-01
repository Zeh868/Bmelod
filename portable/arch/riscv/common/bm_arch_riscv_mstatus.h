/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_riscv_mstatus.h
 * @brief RISC-V 32/64 共享 mstatus / mcause 内联原语
 *
 * 供 `riscv32` 与 `riscv64` 独立静态库共用；通过 `__riscv_xlen` 区分寄存器宽度。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 *
 */
#ifndef BM_ARCH_RISCV_MSTATUS_H
#define BM_ARCH_RISCV_MSTATUS_H

#include <stdint.h>

#if defined(__riscv_xlen) && (__riscv_xlen == 64)
typedef uint64_t bm_arch_riscv_word_t;
#elif defined(__riscv_xlen) && (__riscv_xlen == 32)
typedef uint32_t bm_arch_riscv_word_t;
#else
typedef uintptr_t bm_arch_riscv_word_t;
#endif

/** mstatus.MIE（机器模式全局中断使能） */
#define BM_ARCH_RISCV_MSTATUS_MIE ((bm_arch_riscv_word_t)8)

/**
 * @brief 读取 RISC-V mstatus 寄存器
 *
 * @return 当前 mstatus 值
 */
static inline bm_arch_riscv_word_t bm_arch_riscv_read_mstatus(void) {
    bm_arch_riscv_word_t value;
    __asm volatile ("csrr %0, mstatus" : "=r"(value));
    return value;
}

/**
 * @brief 写入 RISC-V mstatus 寄存器
 *
 * @param value 要恢复的 mstatus 值
 */
static inline void bm_arch_riscv_write_mstatus(bm_arch_riscv_word_t value) {
    __asm volatile ("csrw mstatus, %0" :: "r"(value) : "memory");
}

/**
 * @brief 清除 mstatus.MIE，关闭机器模式全局中断
 */
static inline void bm_arch_riscv_clear_mie(void) {
    __asm volatile ("csrc mstatus, %0" :: "r"(BM_ARCH_RISCV_MSTATUS_MIE) : "memory");
}

/**
 * @brief 判断当前是否处于 RISC-V 中断陷阱上下文
 *
 * @return 非 0 表示 mcause 为中断；0 表示非中断陷阱或普通上下文
 */
static inline int bm_arch_riscv_in_isr(void) {
    bm_arch_riscv_word_t mcause;
    __asm volatile ("csrr %0, mcause" : "=r"(mcause));
    return (mcause >> (sizeof(mcause) * 8u - 1u)) != 0u;
}

#endif /* BM_ARCH_RISCV_MSTATUS_H */

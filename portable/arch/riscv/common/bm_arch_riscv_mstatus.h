/**
 * @file bm_arch_riscv_mstatus.h
 * @brief RISC-V 32/64 共享 mstatus / mcause 内联原语
 *
 * 供 `riscv32` 与 `riscv64` 独立静态库共用；通过 `__riscv_xlen` 区分寄存器宽度。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
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

static inline bm_arch_riscv_word_t bm_arch_riscv_read_mstatus(void) {
    bm_arch_riscv_word_t value;
    __asm volatile ("csrr %0, mstatus" : "=r"(value));
    return value;
}

static inline void bm_arch_riscv_write_mstatus(bm_arch_riscv_word_t value) {
    __asm volatile ("csrw mstatus, %0" :: "r"(value) : "memory");
}

static inline void bm_arch_riscv_clear_mie(void) {
    __asm volatile ("csrc mstatus, %0" :: "r"(BM_ARCH_RISCV_MSTATUS_MIE) : "memory");
}

static inline int bm_arch_riscv_in_isr(void) {
    bm_arch_riscv_word_t mcause;
    __asm volatile ("csrr %0, mcause" : "=r"(mcause));
    return (mcause >> (sizeof(mcause) * 8u - 1u)) != 0u;
}

#endif /* BM_ARCH_RISCV_MSTATUS_H */

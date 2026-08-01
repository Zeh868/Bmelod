/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_riscv_isr_fpu.h
 * @brief RISC-V（RV32IMAC/RV64IMAC）ISR 内浮点安全守卫（恒 no-op）。
 *
 * 供 riscv32、riscv64 目录的 bm_arch_isr_fpu.h 转发引用。
 *
 * @par 平台真相:
 *   本框架 RISC-V 目标编译为 IMAC（见 cmake/toolchain-riscv*-none-elf.cmake：
 *   `-march=rv32imac`/`rv64imac`），不含 F/D 浮点扩展、ABI 为纯整数
 *   （ilp32/lp64），即硬件层面根本没有 FPU 寄存器组。浮点运算全部由编译器
 *   展开为软浮点库调用（libgcc），只使用通用整数寄存器，与普通函数调用无异，
 *   天然被现有 trap 入口的通用寄存器保存覆盖。故本头恒为 no-op。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            新增 RISC-V ISR FPU 守卫（恒 no-op，IMAC 无 F/D 扩展）
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 */
#ifndef BM_ARCH_RISCV_ISR_FPU_H
#define BM_ARCH_RISCV_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 无 F/D 扩展、无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（RISC-V IMAC 无 F/D 扩展，恒 no-op）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容其它架构）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（RISC-V IMAC 无 F/D 扩展，恒 no-op）。
 * @param[in] sa   保存区（未使用）。
 * @param[in] prev enter 返回值（未使用）。
 */
static inline void bm_arch_isr_fpu_exit(void *sa, unsigned prev)
{
    (void)sa;
    (void)prev;
}

#ifdef __cplusplus
}
#endif

#endif /* BM_ARCH_RISCV_ISR_FPU_H */

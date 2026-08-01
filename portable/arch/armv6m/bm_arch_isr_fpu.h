/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_isr_fpu.h
 * @brief ARMv6-M 架构 ISR 内浮点安全守卫（Cortex-M0/M0+ 恒为 no-op）。
 *
 * @par 平台真相:
 *   Cortex-M0/M0+ 内核不含 VFP 浮点协处理器（ISA 层面无该硬件单元），不存在
 *   "ISR 内浮点是否安全"的问题——浮点运算全部由编译器展开为软浮点（libgcc
 *   调用），仅使用通用整数寄存器，与普通函数调用无异，天然被现有 ISR 入口的
 *   通用寄存器保存覆盖。故本头恒为 no-op，且与 Cortex-M4/M7/M33（有 FPU、
 *   靠硬件自动/lazy stacking 保护）的 no-op 原因不同，不与其共享实现。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            新增 ARMv6-M ISR FPU 守卫（恒 no-op，无 FPU 硬件）
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 */
#ifndef BM_ARCH_ISR_FPU_H
#define BM_ARCH_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 无 FPU 硬件、无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（ARMv6-M 无 FPU，恒 no-op）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容其它架构）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（ARMv6-M 无 FPU，恒 no-op）。
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

#endif /* BM_ARCH_ISR_FPU_H */

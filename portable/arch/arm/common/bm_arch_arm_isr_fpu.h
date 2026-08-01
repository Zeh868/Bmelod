/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_arm_isr_fpu.h
 * @brief ARM Cortex-M（含 FPU 型号）ISR 内浮点安全守卫（恒 no-op）。
 *
 * 供 armv7em、armv8m_main 目录的 bm_arch_isr_fpu.h 转发引用。
 *
 * @par 平台真相:
 *   Cortex-M4F/M7/M33/M55 等含 FPU 型号，异常入口硬件自动 stacking（基本帧
 *   xPSR/PC/LR/R12/R3-R0）叠加可选 lazy FPU stacking（扩展帧 S0-S15/FPSCR，
 *   经 FPCCR.LSPEN 使能，首条浮点指令触发时才真正压栈）：异常发生瞬间浮点
 *   现场即由硬件保证不被破坏，异常返回时同步恢复，全程无需软件介入。故本头
 *   恒为 no-op，且该 no-op 的正确性由硬件机制保证（与 armv6m 因"无 FPU 硬件"
 *   而 no-op 的原因不同，armv7a 因"裸机向量未保存 VFP 现场"而 no-op 待实现
 *   的原因也不同）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            新增 Cortex-M ISR FPU 守卫（恒 no-op，硬件自动/lazy stacking 承担）
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 */
#ifndef BM_ARCH_ARM_ISR_FPU_H
#define BM_ARCH_ARM_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 硬件自动/lazy stacking 承担浮点现场，无软件保存区，占位大小为 1。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（Cortex-M 硬件自动/lazy stacking 承担，恒 no-op）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容其它架构）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（Cortex-M 硬件自动/lazy stacking 承担，恒 no-op）。
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

#endif /* BM_ARCH_ARM_ISR_FPU_H */

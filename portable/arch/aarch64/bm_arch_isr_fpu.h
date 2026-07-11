/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_isr_fpu.h
 * @brief AArch64 架构 ISR 内浮点安全守卫（当前 no-op，占位待评估）。
 *
 * @par 平台真相:
 *   QEMU aarch64 virt 裸机 IRQ 入口（boot/qemu_aarch64_smp/startup_qemu_aarch64_smp.S
 *   `aarch64_irq_spx`）仅 `stp` 保存少量通用寄存器（x0-x7/x29/x30），不保存
 *   SIMD/FP 寄存器（V0-V31）现场，ISR 回调因此禁止浮点/NEON 运算。是否需要
 *   补全现场保存、以及 CPACR_EL1/CPTR_EL2 陷入门控如何配置，待评估（本框架
 *   AArch64 路径当前仅用于仿真回归，尚无真实浮点 ISR 回调需求）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            新增 AArch64 ISR FPU 守卫占位（no-op，待评估）
 */
#ifndef BM_ARCH_ISR_FPU_H
#define BM_ARCH_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（AArch64 当前为 no-op，见文件头「平台真相」）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容其它架构）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（AArch64 当前为 no-op）。
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

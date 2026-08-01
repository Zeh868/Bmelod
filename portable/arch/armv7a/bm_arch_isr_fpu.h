/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_isr_fpu.h
 * @brief ARMv7-A 架构 ISR 内浮点安全守卫（当前 no-op，占位待实现）。
 *
 * @par 平台真相:
 *   QEMU cortex-a virt 裸机 IRQ 入口（boot/qemu_cortexa_smp/startup_qemu_cortexa_smp.S:52-56
 *   `cortexa_irq_handler`）仅 `stmfd sp!, {r0-r12, lr}` 保存通用寄存器现场，
 *   不保存 VFP 现场（S0-S31/D0-D15），ISR 回调因此禁止浮点运算。
 *
 * @note 真做需实现 VFP 现场 save/restore（`vstmia`/`vldmia` 或等价的 lazy
 *       stacking 逻辑）+ FPEXC 使能位管理，属 TODO。本头暂提供 no-op 占位，
 *       统一调用点接线；未来补齐后，既有调用方无需改动即透明获得保护。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 * 2026-08-01       1.0            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            新增 ARMv7-A ISR FPU 守卫占位（no-op）
 */
#ifndef BM_ARCH_ISR_FPU_H
#define BM_ARCH_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（ARMv7-A 当前为 no-op，见文件头「平台真相」）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容 xtensa 实现）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（ARMv7-A 当前为 no-op）。
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

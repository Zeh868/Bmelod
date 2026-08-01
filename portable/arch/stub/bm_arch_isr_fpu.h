/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_isr_fpu.h
 * @brief 桩架构（CI / 无硬件单元测试）ISR 内浮点安全守卫（恒 no-op）。
 *
 * 桩架构不对应任何真实目标，仅用于 CI 编译烟雾与临界区链接测试，从不在真实
 * 中断上下文执行，守卫恒为 no-op。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            新增桩 ISR FPU 守卫（恒 no-op，CI 无硬件）
 * 2026-08-01       1.0            zeh           补齐 Doxygen 合规元数据
 */
#ifndef BM_ARCH_ISR_FPU_H
#define BM_ARCH_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CI 桩架构无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（桩架构恒 no-op）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容其它架构）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（桩架构恒 no-op）。
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

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_isr_fpu.h
 * @brief 宿主架构（PC 单元测试 / native_sim）ISR 内浮点安全守卫（恒 no-op）。
 *
 * @par 平台真相:
 *   宿主环境的"ISR"是普通函数调用（native/native_mp 用定时器线程或轮询模拟
 *   中断），运行在操作系统的线程上下文中，浮点寄存器现场由内核/线程 ABI
 *   在调度切换时统一管理（如 x86-64 SysV ABI 的 XMM/YMM、Windows x64 调用约
 *   定），与真实裸机 ISR 抢占语义无关，天然安全，无需协处理器守卫。
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
 * 2026-07-11       1.0            zeh            新增宿主 ISR FPU 守卫（恒 no-op，OS 线程上下文天然安全）
 */
#ifndef BM_ARCH_ISR_FPU_H
#define BM_ARCH_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

/**
 * @brief 进入 ISR 浮点临界区（宿主线程上下文天然安全，恒 no-op）。
 * @param[out] sa 保存区（未使用，保留调用形态兼容其它架构）。
 * @return 固定返回 0。
 */
static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
    (void)sa;
    return 0u;
}

/**
 * @brief 退出 ISR 浮点临界区（宿主线程上下文天然安全，恒 no-op）。
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

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_arm_primask.h
 * @brief ARMv6-M / ARMv7-M 共享 primask / IPSR 内联原语
 *
 * 供 `armv6m` 与 `armv7em` 目录共用；不含 basepri（后者在 armv7em 内实现）。
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
#ifndef BM_ARCH_ARM_PRIMASK_H
#define BM_ARCH_ARM_PRIMASK_H

#include "bm/common/bm_types.h"

/**
 * @brief 读取 ARM PRIMASK 寄存器
 *
 * @return 当前 PRIMASK 值
 */
static inline uint32_t bm_arch_arm_read_primask(void) {
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r"(primask));
    return primask;
}

/**
 * @brief 写入 ARM PRIMASK 寄存器
 *
 * @param primask 要恢复的 PRIMASK 值
 */
static inline void bm_arch_arm_write_primask(uint32_t primask) {
    __asm volatile ("msr primask, %0" :: "r"(primask) : "memory");
}

/**
 * @brief 判断当前是否处于异常或中断上下文
 *
 * @return 非 0 表示处于异常或中断上下文；0 表示线程上下文
 */
static inline int bm_arch_arm_in_isr(void) {
    uint32_t ipsr;
    __asm volatile ("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0u;
}

#endif /* BM_ARCH_ARM_PRIMASK_H */

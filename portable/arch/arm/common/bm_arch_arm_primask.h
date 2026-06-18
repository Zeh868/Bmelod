/**
 * @file bm_arch_arm_primask.h
 * @brief ARMv6-M / ARMv7-M 共享 primask / IPSR 内联原语
 *
 * 供 `armv6m` 与 `armv7em` 目录共用；不含 basepri（后者在 armv7em 内实现）。
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
#ifndef BM_ARCH_ARM_PRIMASK_H
#define BM_ARCH_ARM_PRIMASK_H

#include "bm/common/bm_types.h"

static inline uint32_t bm_arch_arm_read_primask(void) {
    uint32_t primask;
    __asm volatile ("mrs %0, primask" : "=r"(primask));
    return primask;
}

static inline void bm_arch_arm_write_primask(uint32_t primask) {
    __asm volatile ("msr primask, %0" :: "r"(primask) : "memory");
}

static inline int bm_arch_arm_in_isr(void) {
    uint32_t ipsr;
    __asm volatile ("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0u;
}

#endif /* BM_ARCH_ARM_PRIMASK_H */

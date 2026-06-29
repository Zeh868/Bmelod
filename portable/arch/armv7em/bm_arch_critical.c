/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_critical.c
 * @brief ARMv7E-M 临界区实现（primask + basepri 优先级掩码）
 *
 * Cortex-M4/M4F/M7 等：支持 BM_HAL_HAS_PRIORITY_MASK 与 NVIC 优先级阈值。
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
#ifndef BM_HAL_HAS_PRIORITY_MASK
#define BM_HAL_HAS_PRIORITY_MASK 1
#endif

#ifndef __NVIC_PRIO_BITS
#define __NVIC_PRIO_BITS 4
#endif

#include "port/bm_arch_ops.h"
#include "arm/common/bm_arch_arm_primask.h"

static inline uint32_t bm_arch_arm_read_basepri(void) {
    uint32_t basepri;
    __asm volatile ("mrs %0, basepri" : "=r"(basepri));
    return basepri;
}

static inline void bm_arch_arm_write_basepri(uint32_t basepri) {
    __asm volatile ("msr basepri, %0" :: "r"(basepri) : "memory");
}

bm_irq_state_t bm_arch_critical_enter(void) {
    bm_irq_state_t state = (bm_irq_state_t)bm_arch_arm_read_primask();
    __asm volatile ("cpsid i" ::: "memory");
    return state;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    bm_arch_arm_write_primask((uint32_t)state);
}

int bm_arch_in_isr(void) {
    return bm_arch_arm_in_isr();
}

#if BM_HAL_HAS_PRIORITY_MASK
bm_irq_state_t bm_arch_critical_enter_below(uint8_t threshold) {
    bm_irq_state_t packed = (bm_irq_state_t)bm_arch_arm_read_basepri();
    packed |= (bm_irq_state_t)(bm_arch_arm_read_primask() << 8);
    bm_arch_arm_write_basepri((uint32_t)threshold << (8u - __NVIC_PRIO_BITS));
    return packed;
}

void bm_arch_critical_exit_below(bm_irq_state_t previous_state) {
    bm_arch_arm_write_basepri((uint32_t)previous_state & 0xFFu);
    bm_arch_arm_write_primask((uint32_t)((uint32_t)previous_state >> 8));
}
#endif

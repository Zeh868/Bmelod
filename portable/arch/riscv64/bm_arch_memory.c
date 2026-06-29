/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_memory.c
 * @brief RISC-V 64 位内存屏障实现
 *
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
#include "port/bm_arch_ops.h"

void bm_arch_memory_barrier_release(void) {
    __asm volatile ("fence rw, w" ::: "memory");
}

void bm_arch_memory_barrier_full(void) {
    __asm volatile ("fence rw, rw" ::: "memory");
    __asm volatile ("fence.i" ::: "memory");
}

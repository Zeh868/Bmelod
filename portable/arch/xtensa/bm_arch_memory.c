/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_memory.c
 * @brief Xtensa 内存屏障实现（memw）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            Phase 3 正式发布
 *
 */
#include "port/bm_arch_ops.h"

void bm_arch_memory_barrier_release(void) {
    __asm__ volatile ("memw" ::: "memory");
}

void bm_arch_memory_barrier_full(void) {
    __asm__ volatile ("memw" ::: "memory");
}

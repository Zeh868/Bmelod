/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_memory.c
 * @brief stub 架构内存屏障
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
#include "bm/common/bm_atomic_ipc.h"

void bm_arch_memory_barrier_release(void) {
    bm_atomic_ipc_fence_release();
}

void bm_arch_memory_barrier_full(void) {
    bm_atomic_ipc_fence_full();
}

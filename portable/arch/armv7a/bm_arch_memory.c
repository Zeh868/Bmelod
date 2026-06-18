/**
 * @file bm_arch_memory.c
 * @brief ARMv7-A 内存屏障实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */
#include "port/bm_arch_ops.h"
#include "armv7a/bm_arch_portmacro.h"

void bm_arch_memory_barrier_release(void) {
    BM_ARCH_DMB();
}

void bm_arch_memory_barrier_full(void) {
    BM_ARCH_DSB();
    BM_ARCH_ISB();
}

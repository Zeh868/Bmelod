/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_drv_bundle.c
 * @brief 将 arch 层原语组装为 bm_drv_critical_api / bm_drv_memory_api
 *
 * 每个 arch 静态库链接本文件一份，导出框架 HAL 分发层所需单例符号。
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
#define BM_DRV_CRITICAL_API
#define BM_DRV_MEMORY_API

#include "bm_drv_critical.h"
#include "bm_drv_memory.h"
#include "port/bm_arch_ops.h"

const struct bm_critical_driver_api bm_drv_critical_api = {
    bm_arch_critical_enter,
    bm_arch_critical_exit,
    bm_arch_in_isr,
#if BM_HAL_HAS_PRIORITY_MASK
    bm_arch_critical_enter_below,
    bm_arch_critical_exit_below,
#endif
};

const struct bm_memory_driver_api bm_drv_memory_api = {
    bm_arch_memory_barrier_release,
    bm_arch_memory_barrier_full,
};

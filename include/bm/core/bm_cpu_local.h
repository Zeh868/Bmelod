/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_cpu_local.h
 * @brief CPU 本地访问辅助宏
 *
 * 按 CPU 路由的本地访问路径在 `BM_CONFIG_CPU_COUNT > 1` 时自动启用；
 * 单 CPU 配置（含本仓默认）下恒为 0。可在包含本头前显式定义
 * `BM_CPU_LOCAL_ENABLE_ROUTE` 覆盖该推导。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-16       1.0            zeh            新增 CPU 本地访问辅助宏
 */
#ifndef BM_CPU_LOCAL_H
#define BM_CPU_LOCAL_H

#include "bm/common/bm_types.h"
#include "hal/bm_hal_cpu.h"

/**
 * @brief 是否启用按 CPU 路由的本地访问路径
 *
 * 由 `BM_CONFIG_CPU_COUNT` 推导：> 1 时启用，否则关闭（单 CPU 配置恒 0）。
 */
#ifndef BM_CPU_LOCAL_ENABLE_ROUTE
#if defined(BM_CONFIG_CPU_COUNT) && (BM_CONFIG_CPU_COUNT > 1u)
#define BM_CPU_LOCAL_ENABLE_ROUTE  1
#else
#define BM_CPU_LOCAL_ENABLE_ROUTE  0
#endif
#endif

#ifndef BM_CACHE_ALIGNAS
#if defined(__GNUC__) || defined(__clang__)
#define BM_CACHE_ALIGNAS(n)  __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define BM_CACHE_ALIGNAS(n)  __declspec(align(n))
#else
#define BM_CACHE_ALIGNAS(n)
#endif
#endif

/** @brief 当前逻辑 CPU。 */
#ifndef BM_CPU_THIS
#define BM_CPU_THIS()  bm_hal_cpu_id()
#endif

/** @brief 任意 CPU owner 标记。 */
#ifndef BM_CPU_ANY
#define BM_CPU_ANY  0xFFu
#endif

/**
 * @brief 判断当前 CPU 编号是否在配置范围内。
 *
 * @return 非 0 表示有效
 */
static inline int bm_cpu_local_valid(void) {
    return BM_CPU_THIS() < BM_CONFIG_CPU_COUNT;
}

#endif /* BM_CPU_LOCAL_H */

/**
 * @file bm_mp_cpu.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 提供 `BM_CPU_THIS()`、`bm_mp_cpu_valid()` 及与 IPC 布局一致的
 * `BM_CACHE_ALIGNAS`。
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
#ifndef BM_MP_CPU_H
#define BM_MP_CPU_H

#include "bm/mp/bm_mp_types.h"
#include "bm/common/bm_types.h"
#include "hal/bm_hal_cpu.h"

/** 当前逻辑 CPU 索引（须与分区表 owner 校验一致） */
#define BM_CPU_THIS()  bm_hal_cpu_id()

/**
 * @brief 当前 CPU 索引是否在配置范围内
 *
 * @return 非 0 表示有效
 */
static inline int bm_mp_cpu_valid(void) {
    return BM_CPU_THIS() < BM_CONFIG_CPU_COUNT;
}

/**
 * @brief 要求当前 CPU 合法，否则返回 BM_ERR_INVALID（fail-closed）
 */
static inline int bm_mp_cpu_require_valid(void) {
    return bm_mp_cpu_valid() ? BM_OK : BM_ERR_INVALID;
}

#endif /* BM_MP_CPU_H */

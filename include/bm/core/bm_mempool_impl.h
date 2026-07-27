/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_mempool_impl.h
 * @brief bm_mempool 静态存储分配内部头
 *
 * 本头文件提供 `BM_MEMPOOL_DEFINE` 宏，用于在编译期为内存池实例静态分配
 * 位图与对象存储。该宏涉及内部存储布局，不属于公开 API 兼容性承诺范围，
 * 应用方如需使用请 include 本内部头。
 *
 * @note 公开头 `bm_mempool.h` 仅保留类型定义与函数声明。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            从 bm_mempool.h 迁出静态分配宏
 *
 */
#ifndef BM_MEMPOOL_IMPL_H
#define BM_MEMPOOL_IMPL_H

#include "bm/core/bm_mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 静态定义内存池实例
 *  示例：BM_MEMPOOL_DEFINE(my_pool, my_type_t, 16); */
#define BM_MEMPOOL_DEFINE(name, type, cnt) \
    static uint32_t _bm_pool_bitmap_##name[((cnt) + 31U) / 32U] = {0}; \
    static type _bm_pool_storage_##name[(cnt)]; \
    static bm_mempool_t name = { \
        .bitmap = _bm_pool_bitmap_##name, \
        .pool = _bm_pool_storage_##name, \
        .obj_size = sizeof(type), \
        .count = (cnt), \
        .bitmap_words = ((cnt) + 31U) / 32U, \
        .lock = BM_ATOMIC_IPC_U32_INIT(0u) \
    }

#ifdef __cplusplus
}
#endif

#endif /* BM_MEMPOOL_IMPL_H */

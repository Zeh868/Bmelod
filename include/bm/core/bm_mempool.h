/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_mempool.h
 * @brief 固定大小对象内存池
 *
 * 基于位图追踪空闲槽位，支持 O(n) 分配与释放。
 *
 * @core_affinity 实例约束
 * 共享场景采用一次性 try-lock，竞争时立即失败，不进行无界自旋。
 * 硬实时路径应将池耗尽/竞争作为显式背压处理。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_MEMPOOL_H
#define BM_MEMPOOL_H

#include "bm/common/bm_types.h"
#include "bm/common/bm_atomic_ipc.h"

#include <stddef.h>
#include <stdint.h>

/** 内存池控制块 */
typedef struct {
    uint32_t *bitmap;
    void     *pool;
    size_t    obj_size;
    uint32_t  count;
    uint32_t  bitmap_words;
    bm_atomic_ipc_u32_t lock;
} bm_mempool_t;

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

/**
 * @brief 从内存池分配一个对象
 *
 * @param pool 内存池控制块指针
 * @return 对象指针（已清零）；池满时返回 NULL
 */
void *bm_mempool_alloc(bm_mempool_t *pool);

/**
 * @brief 将对象归还内存池
 *
 * @param pool 内存池控制块指针
 * @param obj 待释放的对象指针
 */
void bm_mempool_free(bm_mempool_t *pool, void *obj);

/**
 * @brief 将内存池位图复位为全部空闲（不修改池内对象内容）
 *
 * 仅用于测试或受控停机；调用方须保证无悬空指针仍引用池中对象。
 *
 * @param pool 内存池控制块指针
 */
void bm_mempool_reset(bm_mempool_t *pool);

#endif /* BM_MEMPOOL_H */

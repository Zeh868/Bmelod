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
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-07-27       1.1            zeh            将 BM_MEMPOOL_DEFINE 迁到 bm_mempool_impl.h
 * 2026-07-28       1.2            zeh            新增可观测的非阻塞 try_free 接口
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

/**
 * @brief 从内存池分配一个对象
 *
 * @param pool 内存池控制块指针
 * @return 对象指针（已清零）；池满时返回 NULL
 */
void *bm_mempool_alloc(bm_mempool_t *pool);

/**
 * @brief 尝试将对象归还内存池（单次非阻塞尝试）
 *
 * 共享池争用时立即返回 BM_ERR_BUSY，不会自旋等待。需要可靠归还时，调用方应
 * 在其自身的非实时预算内根据返回值安排有限重试或故障处理。
 *
 * @param pool 内存池控制块指针
 * @param obj 待释放的对象指针
 * @return BM_OK 成功；BM_ERR_BUSY 表示锁争用；BM_ERR_INVALID 表示对象或池非法
 */
int bm_mempool_try_free(bm_mempool_t *pool, void *obj);

/**
 * @brief 将对象归还内存池
 *
 * 此兼容包装仅调用一次 bm_mempool_try_free 后立即返回；需要观测并处理争用
 * 失败时必须改用 bm_mempool_try_free。
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

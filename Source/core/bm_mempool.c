/**
 * @file bm_mempool.c
 * @brief 固定大小对象内存池实现
 *
 * 位图标记空闲槽，临界区保护分配/释放。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            SIL-2 溢出与双释放检测
 * 2026-06-26       1.2            zeh            修复 free 跨核争用静默丢弃→自旋等锁
 *
 */
#include "bm_mempool.h"
#include "bm_critical_wrap.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_log.h"
#include <string.h>

#if BM_CPU_LOCAL_ENABLE_ROUTE
static inline int mempool_lock(bm_mempool_t *pool, bm_irq_state_t *s) {
    *s = BM_CRITICAL_ENTER();
    /*
     * try-lock 而非阻塞：mempool 争用表示 WCET 预算已破，立即失败
     * 比自旋更可预测，便于 hard RT 剖面 fail-fast。
     */
    if (bm_atomic_ipc_exchange_u32(&pool->lock, 1u) != 0u) {
        BM_CRITICAL_EXIT(*s);
        return BM_ERR_BUSY;
    }
    return BM_OK;
}

static inline void mempool_unlock(bm_mempool_t *pool, bm_irq_state_t s) {
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&pool->lock, 0u);
    BM_CRITICAL_EXIT(s);
}

/*
 * 阻塞式获锁：仅供 free 使用——free 必须成功完成，丢弃 free 会永久泄漏槽位。
 * 不在持本核 IRQ 屏蔽期间自旋：每次尝试失败即退出临界区，让对方核完成其
 * 有界临界区并释放锁、也让本核 ISR 推进，再重试。对方核仅在有界位图操作
 * 期间持锁，故自旋有界，WCET = N_cores × 单次临界区时长，仍可分析。
 * alloc 保持 fail-fast（返回 NULL，调用方可恢复），不走此路径。
 */
static inline void mempool_lock_blocking(bm_mempool_t *pool, bm_irq_state_t *s) {
    for (;;) {
        *s = BM_CRITICAL_ENTER();
        if (bm_atomic_ipc_exchange_u32(&pool->lock, 1u) == 0u) {
            return;
        }
        BM_CRITICAL_EXIT(*s);
    }
}

#define MEMPOOL_LOCK(p, s)          mempool_lock((p), (s))
#define MEMPOOL_UNLOCK(p, s)        mempool_unlock((p), (s))
#define MEMPOOL_LOCK_BLOCKING(p, s) mempool_lock_blocking((p), (s))
#else
static inline int mempool_lock(bm_mempool_t *pool, bm_irq_state_t *s) {
    (void)pool;
    *s = BM_CRITICAL_ENTER();
    return BM_OK;
}

#define MEMPOOL_LOCK(p, s)          mempool_lock((p), (s))
#define MEMPOOL_UNLOCK(p, s)        BM_CRITICAL_EXIT(s)
/* 非路由单核：mempool_lock 恒成功并屏蔽本核 IRQ，视为阻塞式（无争用）。 */
#define MEMPOOL_LOCK_BLOCKING(p, s) ((void)mempool_lock((p), (s)))
#endif

/**
 * @brief 校验内存池描述符基本字段
 */
static int mempool_validate_pool(const bm_mempool_t *pool) {
    uint32_t min_words;
    size_t total_bytes;

    if (!pool || !pool->bitmap || !pool->pool || pool->obj_size == 0u ||
        pool->count == 0u) {
        return BM_ERR_INVALID;
    }
    min_words = pool->count / 32u;
    if ((pool->count % 32u) != 0u) {
        min_words++;
    }
    if (pool->bitmap_words < min_words) {
        return BM_ERR_INVALID;
    }
    if (pool->obj_size > (SIZE_MAX / pool->count)) {
        return BM_ERR_INVALID;
    }
    total_bytes = pool->obj_size * pool->count;
    if (total_bytes > (size_t)UINTPTR_MAX ||
        (uintptr_t)pool->pool >
            UINTPTR_MAX - (uintptr_t)total_bytes) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/** @brief 计算容纳 count 个对象所需的位图字数 */
static uint32_t mempool_required_bitmap_words(const bm_mempool_t *pool) {
    uint32_t words = pool->count / 32u;

    return words + (((pool->count % 32u) != 0u) ? 1u : 0u);
}

/**
 * @brief 计算池内存上界（溢出安全）
 */
static int mempool_pool_end(const bm_mempool_t *pool, uintptr_t *end_out) {
    size_t total_bytes;
    uintptr_t pool_start;

    if (mempool_validate_pool(pool) != BM_OK || !end_out) {
        return BM_ERR_INVALID;
    }
    total_bytes = pool->obj_size * pool->count;
    pool_start = (uintptr_t)pool->pool;
    *end_out = pool_start + total_bytes;
    return BM_OK;
}

/**
 * @brief 从内存池分配一个固定大小对象
 *
 * @param pool 内存池描述符指针
 * @return 对象指针；失败返回 NULL
 */
void *bm_mempool_alloc(bm_mempool_t *pool) {
    void *obj = NULL;
    uint32_t allocated_idx = 0u;
    uint32_t bitmap_words;

    if (mempool_validate_pool(pool) != BM_OK) {
        BM_LOGE("mempool", "alloc invalid pool");
        return NULL;
    }

    bitmap_words = mempool_required_bitmap_words(pool);
    bm_irq_state_t s;
    if (MEMPOOL_LOCK(pool, &s) != BM_OK) {
        BM_LOGW("mempool", "alloc contention");
        return NULL;
    }
    for (uint32_t w = 0u; w < bitmap_words; w++) {
        if (pool->bitmap[w] != 0xFFFFFFFFU) {
            for (int b = 0; b < 32; b++) {
                if (!(pool->bitmap[w] & (1U << b))) {
                    uint32_t idx = w * 32u + (uint32_t)b;
                    if (idx >= pool->count) {
                        break;
                    }

                    pool->bitmap[w] |= (1U << b);
                    obj = (uint8_t *)pool->pool +
                          (size_t)idx * pool->obj_size;
                    allocated_idx = idx;
                    /*
                     * 清零在锁内完成：位图置位与对象清零成为单一原子区间。
                     * 否则在掩码模式下，同核 HRT ISR 可抢占 unlock 与 memset
                     * 之间，并通过该已置位槽位观察到部分清零对象（撕裂）。
                     * 对象大小固定且有界，临界区时长仍确定。
                     */
                    memset(obj, 0, pool->obj_size);
                    MEMPOOL_UNLOCK(pool, s);
                    BM_LOGT("mempool", "alloc slot %u",
                            (unsigned)allocated_idx);
                    return obj;
                }
            }
        }
    }
    MEMPOOL_UNLOCK(pool, s);
    BM_LOGW("mempool", "alloc pool exhausted");
    return NULL;
}

/**
 * @brief 将对象归还内存池
 *
 * @param pool 内存池描述符指针
 * @param obj 待释放的对象指针
 */
void bm_mempool_free(bm_mempool_t *pool, void *obj) {
    uintptr_t pool_end = 0u;
    uintptr_t obj_address;
    uintptr_t offset;
    uint32_t idx;
    uint32_t word;
    uint32_t bit;

    if (mempool_validate_pool(pool) != BM_OK || !obj) {
        BM_LOGE("mempool", "free invalid args");
        return;
    }
    if (mempool_pool_end(pool, &pool_end) != BM_OK) {
        BM_LOGE("mempool", "free pool size overflow");
        return;
    }

    obj_address = (uintptr_t)obj;
    if (obj_address < (uintptr_t)pool->pool || obj_address >= pool_end) {
        BM_LOGE("mempool", "free out of range ptr=%p", obj);
        return;
    }

    offset = obj_address - (uintptr_t)pool->pool;
    if ((offset % pool->obj_size) != 0u) {
        BM_LOGE("mempool", "free misaligned ptr=%p", obj);
        return;
    }
    idx = (uint32_t)(offset / pool->obj_size);
    if (idx >= pool->count) {
        BM_LOGE("mempool", "free idx out of range ptr=%p", obj);
        return;
    }

    word = idx / 32u;
    bit = idx % 32u;
    if (word >= pool->bitmap_words) {
        BM_LOGE("mempool", "free bitmap word overflow idx=%u", (unsigned)idx);
        return;
    }

    bm_irq_state_t s;
    MEMPOOL_LOCK_BLOCKING(pool, &s);   /* free 必须成功：争用时自旋等锁，绝不丢弃 */
    if (!(pool->bitmap[word] & (1U << bit))) {
        MEMPOOL_UNLOCK(pool, s);
        BM_LOGE("mempool", "free double-free slot %u", (unsigned)idx);
        return;
    }
    pool->bitmap[word] &= ~(1U << bit);
    MEMPOOL_UNLOCK(pool, s);
    BM_LOGT("mempool", "free slot %u", (unsigned)idx);
}

/**
 * @brief 重置内存池，释放所有已分配对象
 *
 * @param pool 内存池描述符指针
 */
void bm_mempool_reset(bm_mempool_t *pool) {
    uint32_t bitmap_words;

    if (mempool_validate_pool(pool) != BM_OK) {
        BM_LOGE("mempool", "reset invalid pool");
        return;
    }

    bitmap_words = mempool_required_bitmap_words(pool);
    bm_irq_state_t s;
    if (MEMPOOL_LOCK(pool, &s) != BM_OK) {
        BM_LOGW("mempool", "reset contention");
        return;
    }
    memset(pool->bitmap, 0, (size_t)bitmap_words * sizeof(uint32_t));
    MEMPOOL_UNLOCK(pool, s);
    BM_LOGT("mempool", "reset all slots free");
}

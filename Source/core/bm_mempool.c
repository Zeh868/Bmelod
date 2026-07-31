/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_mempool.c
 * @brief 固定大小对象内存池实现
 *
 * 位图标记空闲槽，临界区保护分配/释放。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-10       1.1            zeh            SIL-2 溢出与双释放检测
 * 2026-06-26       1.2            zeh            修复 free 跨核争用静默丢弃→自旋等锁
 * 2026-07-28       1.3            zeh            free 改为单次 try-lock，消除无界自旋
 * 2026-07-30       1.4            zeh            掩码模式 HRT 级 ISR 调用运行期
 *                                                fail-closed；统一与 bm_event.c
 *                                                的 ISR 契约口径
 * 2026-07-31       1.5            zeh            改用共享的
 *                                                BM_SRT_QUEUE_API_FORBIDDEN()；
 *                                                拒绝日志改 log-once
 *
 */
#include "bm_mempool.h"
#include "bm_critical_wrap.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_log.h"
#include <string.h>

#if BM_CPU_LOCAL_ENABLE_ROUTE
/**
 * @brief 单次尝试获取共享内存池锁
 *
 * @param pool 内存池描述符指针
 * @param s 保存临界区状态的输出指针
 * @return BM_OK 成功；BM_ERR_BUSY 表示锁被其他核心持有
 */
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

/**
 * @brief 释放共享内存池锁并退出临界区
 *
 * @param pool 内存池描述符指针
 * @param s 先前保存的临界区状态
 */
static inline void mempool_unlock(bm_mempool_t *pool, bm_irq_state_t s) {
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&pool->lock, 0u);
    BM_CRITICAL_EXIT(s);
}

#define MEMPOOL_LOCK(p, s)          mempool_lock((p), (s))
#define MEMPOOL_UNLOCK(p, s)        mempool_unlock((p), (s))
#else
/**
 * @brief 单核模式下进入内存池临界区
 *
 * @param pool 未使用的内存池描述符指针
 * @param s 保存临界区状态的输出指针
 * @return 恒为 BM_OK
 */
static inline int mempool_lock(bm_mempool_t *pool, bm_irq_state_t *s) {
    (void)pool;
    *s = BM_CRITICAL_ENTER();
    return BM_OK;
}

#define MEMPOOL_LOCK(p, s)          mempool_lock((p), (s))
#define MEMPOOL_UNLOCK(p, s)        BM_CRITICAL_EXIT(s)
#endif

/**
 * @brief HRT 级上下文拒绝服务时输出一次诊断日志
 *
 * 掩码模式下 BM_CRITICAL_ENTER() 仅屏蔽低于 HRT 阈值的中断，HRT 级（>= 阈值）
 * ISR 与 SRT 路径不互斥。按"确定性流式 ISR 安全契约"（见 bm_event.c 注释），
 * HRT 级上下文不得调用 event/ultra/mempool API，本模块各入口据
 * BM_SRT_QUEUE_API_FORBIDDEN() fail-closed。
 *
 * 该路径只可能在 HRT 级 ISR 内命中，日志按"首次拒绝"输出一次即止：bm_log
 * 在非 ring 配置下直写 UART 属无界 I/O，不可留在 HRT 路径上反复执行。
 *
 * @param op 被拒绝的操作名（alloc/free/reset）
 */
static void mempool_log_hrt_reject_once(const char *op) {
    static volatile int logged;

    if (!logged) {
        logged = 1;
        BM_LOGE("mempool", "%s from HRT-level ISR rejected", op);
    }
}

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
    if (BM_SRT_QUEUE_API_FORBIDDEN()) {
        mempool_log_hrt_reject_once("alloc");
        return NULL;
    }

    bitmap_words = mempool_required_bitmap_words(pool);
    bm_irq_state_t s;
    if (MEMPOOL_LOCK(pool, &s) != BM_OK) {
        BM_LOGW("mempool", "alloc contention");
        return NULL;
    }
    /*
     * [F-3 WCET] 首适配线性扫描，WCET = O(count)——池近满且空槽在末位时须
     * 全量遍历所有位图字；RTA 预算须按满扫描最坏情况（即 count 次）取值。
     * 实测典型远小于最坏；但硬实时剖面下禁止用平均情况替代最坏情况估算。
     */
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
                     * 清零在锁内完成：位图置位与对象清零成为单一有界原子区间，
                     * 保证"alloc 返回即清零"的契约对任何合法调用方成立——
                     * 包括掩码模式下允许调用本 API 的低于 HRT 阈值的 ISR。
                     * HRT 级（>= 阈值）ISR 按契约禁止调用本 API（见 bm_event.c
                     * "确定性流式 ISR 安全契约"），掩码模式下由入口处的
                     * BM_SRT_QUEUE_API_FORBIDDEN() 运行期拦截。
                     * 对象大小固定且有界，临界区时长仍确定。
                     *
                     * [F-1 IRQ-off 窗口] BM_CRITICAL 关中断的最坏时长 ∝ 最大
                     * 池对象 obj_size，会抬高全局最坏中断延迟（IRQ-off latency）；
                     * 硬实时剖面须对大对象池的 obj_size 设上限，并据此在系统级
                     * IRQ-off 预算表中登记该临界区的最坏窗口，以确保关键 ISR
                     * 截止期可达（见 F-2：free 阻塞 WCET 同源）。
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
 * @brief 尝试将对象归还内存池
 *
 * @param pool 内存池描述符指针
 * @param obj 待释放的对象指针
 * @return BM_OK 成功；BM_ERR_BUSY 锁争用；BM_ERR_INVALID 对象或池非法
 */
int bm_mempool_try_free(bm_mempool_t *pool, void *obj) {
    uintptr_t pool_end = 0u;
    uintptr_t obj_address;
    uintptr_t offset;
    uint32_t idx;
    uint32_t word;
    uint32_t bit;

    if (mempool_validate_pool(pool) != BM_OK || !obj) {
        BM_LOGE("mempool", "free invalid args");
        return BM_ERR_INVALID;
    }
    if (BM_SRT_QUEUE_API_FORBIDDEN()) {
        mempool_log_hrt_reject_once("free");
        return BM_ERR_BUSY;
    }
    if (mempool_pool_end(pool, &pool_end) != BM_OK) {
        BM_LOGE("mempool", "free pool size overflow");
        return BM_ERR_INVALID;
    }

    obj_address = (uintptr_t)obj;
    if (obj_address < (uintptr_t)pool->pool || obj_address >= pool_end) {
        BM_LOGE("mempool", "free out of range ptr=%p", obj);
        return BM_ERR_INVALID;
    }

    offset = obj_address - (uintptr_t)pool->pool;
    if ((offset % pool->obj_size) != 0u) {
        BM_LOGE("mempool", "free misaligned ptr=%p", obj);
        return BM_ERR_INVALID;
    }
    idx = (uint32_t)(offset / pool->obj_size);
    if (idx >= pool->count) {
        BM_LOGE("mempool", "free idx out of range ptr=%p", obj);
        return BM_ERR_INVALID;
    }

    word = idx / 32u;
    bit = idx % 32u;
    if (word >= pool->bitmap_words) {
        BM_LOGE("mempool", "free bitmap word overflow idx=%u", (unsigned)idx);
        return BM_ERR_INVALID;
    }

    bm_irq_state_t s;
    if (MEMPOOL_LOCK(pool, &s) != BM_OK) {
        BM_LOGW("mempool", "free contention");
        return BM_ERR_BUSY;
    }
    if (!(pool->bitmap[word] & (1U << bit))) {
        MEMPOOL_UNLOCK(pool, s);
        BM_LOGE("mempool", "free double-free slot %u", (unsigned)idx);
        return BM_ERR_INVALID;
    }
    pool->bitmap[word] &= ~(1U << bit);
    MEMPOOL_UNLOCK(pool, s);
    BM_LOGT("mempool", "free slot %u", (unsigned)idx);
    return BM_OK;
}

/**
 * @brief 兼容接口：单次尝试归还对象后立即返回
 *
 * @param pool 内存池描述符指针
 * @param obj 待释放的对象指针
 */
void bm_mempool_free(bm_mempool_t *pool, void *obj) {
    (void)bm_mempool_try_free(pool, obj);
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
    if (BM_SRT_QUEUE_API_FORBIDDEN()) {
        mempool_log_hrt_reject_once("reset");
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

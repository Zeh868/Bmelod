/**
 * @file bm_stream.c
 * @brief 静态零拷贝块流实现
 *
 * 实现 bm_stream 生产者/消费者 API、过载策略（drop-newest/drop-oldest）与
 * 运行统计计数。单生产者单消费者，零堆、零拷贝交接。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 bm_stream_mark_late
 * 2026-06-14       1.2            zeh            commit/drain 解耦
 * 2026-06-14       1.2            zeh            按 CPU 约束 on_ready/drain 同步路径
 * 2026-06-15       1.4            zeh            默认禁用 ready 回调并清理旧时间戳
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm_stream.h"
#include "bm_log.h"
#include "hal/bm_hal_critical.h"
#include "hal/bm_hal_cache.h"
#include "bm/core/bm_cpu_local.h"

#include <string.h>

/*
 * Stream 临界区使用全关中断（bm_hal_critical_enter），而非
 * bm_critical_wrap.h 中可能带优先级掩码的 BM_CRITICAL_ENTER。
 *
 * 原因：环索引与块状态转换虽短但关乎正确性；HRT ISR 抢占
 * commit/drain 可破坏环状态（如双重 release、索引不一致）。
 * 全关中断具确定性，临界区限于对索引/标志的 O(1) 操作。
 *
 * 使用显式局部宏 BM_STREAM_CRITICAL_ENTER/EXIT 在各调用点标明
 * 语义差异，而非静默重定义框架级 BM_CRITICAL_ENTER/EXIT。
 */
#define BM_STREAM_CRITICAL_ENTER()  bm_hal_critical_enter()
#define BM_STREAM_CRITICAL_EXIT(state) bm_hal_critical_exit(state)

#ifndef BM_CONFIG_STREAM_ENABLE_LEGACY_READY_HANDLER
#define BM_CONFIG_STREAM_ENABLE_LEGACY_READY_HANDLER 0
#endif

/** @brief 校验 stream 描述符字段合法 */
static int stream_valid(const bm_stream_t *stream) {
    return (stream != NULL &&
            stream->initialized != 0 &&
            stream->blocks != NULL &&
            stream->block_count >= 2u &&
            stream->block_count <= stream->block_capacity &&
            stream->block_capacity <= BM_CONFIG_STREAM_MAX_BLOCKS);
}

/** @brief 校验 stream 的 owner_cpu 字段合法 */
static int stream_owner_cpu_valid(const bm_stream_t *stream) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    return stream != NULL && stream->owner_cpu < BM_CONFIG_CPU_COUNT;
#else
    (void)stream;
    return 1;
#endif
}

/** @brief 校验当前 CPU 是否为 stream 的属主 */
static int stream_owner_valid(const bm_stream_t *stream) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    return stream_owner_cpu_valid(stream) &&
           BM_CPU_THIS() == stream->owner_cpu;
#else
    (void)stream;
    return 1;
#endif
}

/**
 * @brief 在 stream 的块池中查找指定块
 *
 * @param stream stream 指针
 * @param block 待查找块指针
 * @return 块池中对应块指针；未找到返回 NULL
 */
static bm_block_t *find_block(bm_stream_t *stream, bm_block_t *block) {
    uint32_t i;

    for (i = 0u; i < stream->block_count; ++i) {
        if (&stream->blocks[i] == block) {
            return &stream->blocks[i];
        }
    }
    return NULL;
}

/**
 * @brief 查找第一个处于指定状态的块
 *
 * @param stream stream 指针
 * @param state 目标状态
 * @return 匹配块指针；未找到返回 NULL
 */
static bm_block_t *find_state_locked(bm_stream_t *stream,
                                     bm_block_state_t state) {
    uint32_t i;

    for (i = 0u; i < stream->block_count; ++i) {
        if (bm_block_state_load(&stream->blocks[i]) == state) {
            return &stream->blocks[i];
        }
    }
    return NULL;
}

/**
 * @brief 查找最旧的 READY 块（按 sequence 回绕安全比较）
 */
static bm_block_t *find_oldest_ready_locked(bm_stream_t *stream) {
    bm_block_t *oldest = NULL;
    uint32_t i;

    for (i = 0u; i < stream->block_count; ++i) {
        bm_block_t *b = &stream->blocks[i];
        if (bm_block_state_load(b) == BM_BLOCK_STATE_READY &&
            (oldest == NULL ||
             /*
              * 带符号差值比较 sequence，回绕时仍能选出最旧 READY 块
              *（差值须 < 2^31，由块池大小保证）。
              */
             (int32_t)(b->sequence - oldest->sequence) < 0)) {
            oldest = b;
        }
    }
    return oldest;
}

/**
 * @brief 丢弃最旧的 READY 块（drop-oldest 策略使用）
 */
static void drop_oldest_ready_locked(bm_stream_t *stream) {
    bm_block_t *oldest = find_oldest_ready_locked(stream);

    if (oldest != NULL) {
        bm_block_state_store(oldest, BM_BLOCK_STATE_FREE);
        oldest->valid_bytes = 0u;
        memset(&oldest->timestamp, 0, sizeof(oldest->timestamp));
        stream->stats.drop++;
    }
}

int bm_stream_init(bm_stream_t *stream,
                   void *payloads,
                   uint32_t block_count,
                   uint32_t block_bytes) {
    uint32_t i;
    uint32_t descriptor_capacity;
    uint8_t *base;

    if (!stream || !stream_owner_cpu_valid(stream) || !stream_owner_valid(stream) ||
        !stream->blocks || !payloads || block_count < 2u ||
        block_count > BM_CONFIG_STREAM_MAX_BLOCKS || block_bytes == 0u ||
        block_bytes > (UINT32_MAX / block_count)) {
        return BM_ERR_INVALID;
    }

    descriptor_capacity = stream->block_capacity != 0u
                              ? stream->block_capacity
                              : stream->block_count;
    if (descriptor_capacity < block_count ||
        descriptor_capacity > BM_CONFIG_STREAM_MAX_BLOCKS) {
        return BM_ERR_INVALID;
    }

    base = (uint8_t *)payloads;
    memset(stream->blocks, 0, sizeof(bm_block_t) * block_count);
    for (i = 0u; i < block_count; ++i) {
        stream->blocks[i].data = (void *)(base + (block_bytes * i));
        stream->blocks[i].capacity_bytes = block_bytes;
        bm_block_state_store(&stream->blocks[i], BM_BLOCK_STATE_FREE);
    }
    stream->block_count = block_count;
    stream->block_capacity = descriptor_capacity;
    stream->next_sequence = 0u;
    stream->initialized = 1;
    return BM_OK;
}

void bm_stream_reset(bm_stream_t *stream) {
    bm_irq_state_t irq;
    uint32_t i;

    if (!stream || !stream_owner_valid(stream) || !stream->blocks ||
        stream->block_count > stream->block_capacity ||
        stream->block_capacity > BM_CONFIG_STREAM_MAX_BLOCKS) {
        return;
    }
    irq = BM_STREAM_CRITICAL_ENTER();
    for (i = 0u; i < stream->block_count; ++i) {
        stream->blocks[i].valid_bytes = 0u;
        stream->blocks[i].sequence = 0u;
        memset(&stream->blocks[i].timestamp, 0,
               sizeof(stream->blocks[i].timestamp));
        bm_block_state_store(&stream->blocks[i], BM_BLOCK_STATE_FREE);
    }
    memset(&stream->stats, 0, sizeof(stream->stats));
    stream->next_sequence = 0u;
    BM_STREAM_CRITICAL_EXIT(irq);
}

void bm_stream_set_policy(bm_stream_t *stream, bm_stream_policy_t policy) {
    bm_irq_state_t irq;

    if (!stream || !stream_owner_valid(stream) ||
        (policy != BM_STREAM_POLICY_DROP_NEWEST &&
         policy != BM_STREAM_POLICY_DROP_OLDEST)) {
        return;
    }
    /*
     * 过载策略在 stream init 后不可变：运行期切换 drop-newest ↔ drop-oldest
     * 会改变过载语义，破坏 WCET 分析前提。
     */
    if (stream->initialized) {
        BM_LOGW("stream", "set_policy rejected after init");
        return;
    }
    irq = BM_STREAM_CRITICAL_ENTER();
    stream->policy = policy;
    BM_STREAM_CRITICAL_EXIT(irq);
}

void bm_stream_set_ready_handler(bm_stream_t *stream,
                                 bm_stream_ready_fn_t handler,
                                 void *context) {
    bm_irq_state_t irq;

    if (!stream || !stream_owner_valid(stream)) {
        return;
    }
    /*
     * 确定性流式全剖面统一：禁止 on_ready 同步回调。
     * commit 路径 WCET 不得随 handler 变化；
     * stream drain 统一走主循环 bm_exec_drain_streams。
     */
#if !BM_CONFIG_STREAM_ENABLE_LEGACY_READY_HANDLER
    if (handler != NULL) {
        BM_LOGW("stream", "on_ready handler rejected"
                " (use bm_exec_drain_streams instead)");
        return;
    }
#endif
    /* 显式 legacy 调试剖面仍要求 init 前固定，运行期不可改。 */
    if (stream->initialized) {
        BM_LOGW("stream", "set_ready_handler rejected after init");
        return;
    }
    irq = BM_STREAM_CRITICAL_ENTER();
    stream->on_ready = handler;
    stream->on_ready_context = context;
    BM_STREAM_CRITICAL_EXIT(irq);
}

const bm_stream_stats_t *bm_stream_stats(const bm_stream_t *stream) {
    if (!stream_valid(stream) || !stream_owner_valid(stream)) {
        return NULL;
    }
    return &stream->stats;
}

uint32_t bm_stream_ready_count(const bm_stream_t *stream) {
    bm_irq_state_t irq;
    uint32_t count = 0u;
    uint32_t i;

    if (!stream_valid(stream) || !stream_owner_valid(stream)) {
        return 0u;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    for (i = 0u; i < stream->block_count; ++i) {
        if (bm_block_state_load(&stream->blocks[i]) == BM_BLOCK_STATE_READY) {
            count++;
        }
    }
    BM_STREAM_CRITICAL_EXIT(irq);
    return count;
}

void bm_stream_mark_late(bm_stream_t *stream) {
    bm_irq_state_t irq;

    if (!stream_valid(stream) || !stream_owner_valid(stream)) {
        return;
    }
    irq = BM_STREAM_CRITICAL_ENTER();
    stream->stats.late++;
    BM_STREAM_CRITICAL_EXIT(irq);
}

int bm_stream_producer_acquire(bm_stream_t *stream, bm_block_t **block) {
    bm_irq_state_t irq;
    bm_block_t *slot;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_state_locked(stream, BM_BLOCK_STATE_FREE);
    if (!slot) {
        stream->stats.overrun++;
        if (stream->policy == BM_STREAM_POLICY_DROP_OLDEST) {
            /*
             * drop-oldest：牺牲最旧 READY 块以腾出 FREE 槽，
             * 适合传感器流“保留最新样本”语义。
             */
            drop_oldest_ready_locked(stream);
            slot = find_state_locked(stream, BM_BLOCK_STATE_FREE);
        }
        if (!slot) {
            stream->stats.drop++;
            BM_STREAM_CRITICAL_EXIT(irq);
            return BM_ERR_OVERFLOW;
        }
    }

    slot->valid_bytes = 0u;
    memset(&slot->timestamp, 0, sizeof(slot->timestamp));
    bm_block_state_store(slot, BM_BLOCK_STATE_DMA_OWNED);
    *block = slot;
    BM_STREAM_CRITICAL_EXIT(irq);
    return BM_OK;
}

int bm_stream_producer_commit(bm_stream_t *stream,
                              bm_block_t *block,
                              uint32_t valid_bytes,
                              const bm_timestamp_t *timestamp) {
    bm_irq_state_t irq;
    bm_block_t *slot;
    int cache_rc = BM_OK;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_block(stream, block);
    if (!slot || bm_block_state_load(slot) != BM_BLOCK_STATE_DMA_OWNED) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }
    if (valid_bytes > slot->capacity_bytes) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }

    slot->valid_bytes = valid_bytes;
    slot->sequence = ++stream->next_sequence;
    if (timestamp) {
        slot->timestamp = *timestamp;
    }
    BM_STREAM_CRITICAL_EXIT(irq);

    /*
     * cache 维护在临界区外执行：clean 可能较慢，且与 D-cache 交互
     * 不应持锁阻塞 commit；失败时下方路径回收块防止池泄漏。
     */
    if (slot->data != NULL && valid_bytes > 0u) {
        cache_rc = bm_hal_cache_payload_publish(slot->data, valid_bytes);
    } else {
        bm_atomic_ipc_fence_release();
    }
    if (cache_rc != BM_OK) {
        /*
         * 确定性流式资源回收契约：cache 维护失败时框架内部回收块
         *（DMA_OWNED → FREE）并计 corrupt，避免块永久泄漏耗尽池。
         * 调用方收到错误码即可，无需也不应再调用 producer_abort
         *（块已不在 DMA_OWNED 态）。
         */
        irq = BM_STREAM_CRITICAL_ENTER();
        if (bm_block_state_load(slot) == BM_BLOCK_STATE_DMA_OWNED) {
            slot->valid_bytes = 0u;
            bm_block_state_store(slot, BM_BLOCK_STATE_FREE);
        }
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return cache_rc;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    if (bm_block_state_load(slot) != BM_BLOCK_STATE_DMA_OWNED) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }
    bm_block_state_store(slot, BM_BLOCK_STATE_READY);
    stream->pending_drain = 1u;
    BM_STREAM_CRITICAL_EXIT(irq);
    return BM_OK;
}

int bm_stream_drain(bm_stream_t *stream, uint32_t budget) {
    uint32_t notified = 0u;
    bm_stream_ready_fn_t handler;
    void *handler_context;

    if (bm_hal_in_isr()) {
        return 0;
    }

    if (!stream_valid(stream) || !stream_owner_valid(stream) ||
        budget == 0u || !stream->pending_drain) {
        return 0;
    }

    handler = stream->on_ready;
    handler_context = stream->on_ready_context;
    if (!handler) {
        stream->pending_drain = 0u;
        return 0;
    }

    while (notified < budget) {
        bm_irq_state_t irq;
        bm_block_t *slot;

        irq = BM_STREAM_CRITICAL_ENTER();
        slot = find_oldest_ready_locked(stream);
        if (!slot) {
            stream->pending_drain = 0u;
            BM_STREAM_CRITICAL_EXIT(irq);
            break;
        }
        bm_block_state_store(slot, BM_BLOCK_STATE_PROCESSING);
        BM_STREAM_CRITICAL_EXIT(irq);
        if (slot->data != NULL && slot->valid_bytes > 0u) {
            int cache_rc =
                bm_hal_cache_payload_consume(slot->data, slot->valid_bytes);
            if (cache_rc != BM_OK) {
                irq = BM_STREAM_CRITICAL_ENTER();
                bm_block_state_store(slot, BM_BLOCK_STATE_READY);
                BM_STREAM_CRITICAL_EXIT(irq);
                return notified > 0u ? (int)notified : cache_rc;
            }
        }

        handler(stream, slot, handler_context);

        irq = BM_STREAM_CRITICAL_ENTER();
        if (bm_block_state_load(slot) == BM_BLOCK_STATE_PROCESSING) {
            bm_block_state_store(slot, BM_BLOCK_STATE_READY);
        }
        BM_STREAM_CRITICAL_EXIT(irq);
        notified++;
    }

    if (bm_stream_ready_count(stream) == 0u) {
        stream->pending_drain = 0u;
    }
    return (int)notified;
}

int bm_stream_producer_abort(bm_stream_t *stream, bm_block_t *block) {
    bm_irq_state_t irq;
    bm_block_t *slot;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_block(stream, block);
    if (!slot || bm_block_state_load(slot) != BM_BLOCK_STATE_DMA_OWNED) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }

    slot->valid_bytes = 0u;
    memset(&slot->timestamp, 0, sizeof(slot->timestamp));
    bm_block_state_store(slot, BM_BLOCK_STATE_FREE);
    BM_STREAM_CRITICAL_EXIT(irq);
    return BM_OK;
}

int bm_stream_consumer_acquire(bm_stream_t *stream, bm_block_t **block) {
    bm_irq_state_t irq;
    bm_block_t *slot;
    int cache_rc = BM_OK;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_oldest_ready_locked(stream);
    if (!slot) {
        stream->stats.underrun++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_WOULD_BLOCK;
    }

    bm_block_state_store(slot, BM_BLOCK_STATE_PROCESSING);
    *block = slot;
    BM_STREAM_CRITICAL_EXIT(irq);

    if (slot->data != NULL && slot->valid_bytes > 0u) {
        cache_rc = bm_hal_cache_payload_consume(slot->data, slot->valid_bytes);
    }
    if (cache_rc != BM_OK) {
        irq = BM_STREAM_CRITICAL_ENTER();
        bm_block_state_store(slot, BM_BLOCK_STATE_READY);
        BM_STREAM_CRITICAL_EXIT(irq);
        *block = NULL;
        return cache_rc;
    }
    return BM_OK;
}

int bm_stream_consumer_release(bm_stream_t *stream, bm_block_t *block) {
    bm_irq_state_t irq;
    bm_block_t *slot;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_block(stream, block);
    if (!slot ||
        (bm_block_state_load(slot) != BM_BLOCK_STATE_PROCESSING &&
         bm_block_state_load(slot) != BM_BLOCK_STATE_OUTPUT_READY)) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }

    slot->valid_bytes = 0u;
    memset(&slot->timestamp, 0, sizeof(slot->timestamp));
    bm_block_state_store(slot, BM_BLOCK_STATE_FREE);
    BM_STREAM_CRITICAL_EXIT(irq);
    return BM_OK;
}

int bm_stream_output_acquire(bm_stream_t *stream, bm_block_t **block) {
    bm_irq_state_t irq;
    bm_block_t *slot;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_state_locked(stream, BM_BLOCK_STATE_FREE);
    if (!slot) {
        stream->stats.overrun++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_OVERFLOW;
    }

    slot->valid_bytes = 0u;
    memset(&slot->timestamp, 0, sizeof(slot->timestamp));
    bm_block_state_store(slot, BM_BLOCK_STATE_DMA_OWNED);
    *block = slot;
    BM_STREAM_CRITICAL_EXIT(irq);
    return BM_OK;
}

int bm_stream_output_commit(bm_stream_t *stream,
                            bm_block_t *block,
                            uint32_t valid_bytes,
                            const bm_timestamp_t *timestamp) {
    bm_irq_state_t irq;
    bm_block_t *slot;
    int cache_rc = BM_OK;

    if (!stream_valid(stream) || !stream_owner_valid(stream) || !block) {
        return BM_ERR_INVALID;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    slot = find_block(stream, block);
    if (!slot || bm_block_state_load(slot) != BM_BLOCK_STATE_DMA_OWNED) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }
    if (valid_bytes > slot->capacity_bytes) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }

    slot->valid_bytes = valid_bytes;
    if (timestamp) {
        slot->timestamp = *timestamp;
    }
    BM_STREAM_CRITICAL_EXIT(irq);

    if (slot->data != NULL && valid_bytes > 0u) {
        cache_rc = bm_hal_cache_payload_publish(slot->data, valid_bytes);
    } else {
        bm_atomic_ipc_fence_release();
    }
    if (cache_rc != BM_OK) {
        /*
         * 与 producer_commit 一致的回收契约：cache 失败时框架内部回收块
         *（DMA_OWNED → FREE）并计 corrupt，避免泄漏。
         */
        irq = BM_STREAM_CRITICAL_ENTER();
        if (bm_block_state_load(slot) == BM_BLOCK_STATE_DMA_OWNED) {
            slot->valid_bytes = 0u;
            bm_block_state_store(slot, BM_BLOCK_STATE_FREE);
        }
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return cache_rc;
    }

    irq = BM_STREAM_CRITICAL_ENTER();
    if (bm_block_state_load(slot) != BM_BLOCK_STATE_DMA_OWNED) {
        stream->stats.corrupt++;
        BM_STREAM_CRITICAL_EXIT(irq);
        return BM_ERR_INVALID;
    }
    bm_block_state_store(slot, BM_BLOCK_STATE_OUTPUT_READY);
    BM_STREAM_CRITICAL_EXIT(irq);
    return BM_OK;
}

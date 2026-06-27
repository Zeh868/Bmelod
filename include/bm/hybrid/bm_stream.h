/**
 * @file bm_stream.h
 * @brief 静态零拷贝块流（单生产者单消费者）
 *
 * 与 bm_channel 分离：服务 DMA 大块所有权与 Block/Frame RT，禁止在 SRT 中
 * 误用为消息队列。ISR 仅提交 descriptor，不复制 payload。状态迁移为
 * FREE → DMA_OWNED → READY → PROCESSING → FREE（及 OUTPUT 路径）。
 *
 * @core_affinity owner_cpu 约束
 * 每个 bm_stream 实例绑定到创建时指定的 owner_cpu，仅对应 CPU 可调用
 * producer/consumer API。
 * 传输路径由上层调度完成，不在此头文件暴露具体转发细节。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            commit/drain 解耦；owner_cpu / pending_drain
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_STREAM_H
#define BM_STREAM_H

#include "bm/hybrid/bm_block.h"
#include "bm/common/bm_types.h"
#include "bm/core/bm_cpu_local.h"
#include "bm/core/bm_block_backend.h"

#include <stdint.h>

#ifndef BM_CONFIG_STREAM_MAX_BLOCKS
#define BM_CONFIG_STREAM_MAX_BLOCKS 4u
#endif

typedef enum {
    BM_STREAM_POLICY_DROP_NEWEST = 0,
    BM_STREAM_POLICY_DROP_OLDEST
} bm_stream_policy_t;

typedef struct {
    uint32_t overrun;
    uint32_t underrun;
    uint32_t drop;
    uint32_t late;
    uint32_t corrupt;
} bm_stream_stats_t;

typedef struct bm_stream bm_stream_t;

typedef void (*bm_stream_ready_fn_t)(bm_stream_t *stream,
                                     bm_block_t *block,
                                     void *context);

struct bm_stream {
    bm_block_t          *blocks;
    uint32_t             block_count;
    uint32_t             block_capacity;
    bm_stream_policy_t   policy;
    bm_stream_stats_t    stats;
    bm_stream_ready_fn_t on_ready;
    void                *on_ready_context;
    int                  initialized;
    uint32_t             next_sequence;
    uint8_t              owner_cpu;
    volatile uint8_t     pending_drain;
};

#define BM_STREAM_PAYLOADS(name, type, depth) \
    static type _bm_stream_payload_##name[(depth)]

#define BM_STREAM_BLOCKS(name, depth) \
    static bm_block_t _bm_stream_blocks_##name[(depth)]

#define BM_STREAM_INSTANCE(name, depth) \
    static bm_stream_t name = { \
        .blocks = _bm_stream_blocks_##name, \
        .block_count = (depth), \
        .block_capacity = (depth), \
        .policy = BM_STREAM_POLICY_DROP_NEWEST, \
        .owner_cpu = 0u \
    }

void bm_stream_mark_late(bm_stream_t *stream);

int bm_stream_init(bm_stream_t *stream,
                   void *payloads,
                   uint32_t block_count,
                   uint32_t block_bytes);

void bm_stream_reset(bm_stream_t *stream);

void bm_stream_set_policy(bm_stream_t *stream, bm_stream_policy_t policy);

void bm_stream_set_ready_handler(bm_stream_t *stream,
                                 bm_stream_ready_fn_t handler,
                                 void *context);

const bm_stream_stats_t *bm_stream_stats(const bm_stream_t *stream);

uint32_t bm_stream_ready_count(const bm_stream_t *stream);

int bm_stream_producer_acquire(bm_stream_t *stream, bm_block_t **block);

int bm_stream_producer_commit(bm_stream_t *stream,
                             bm_block_t *block,
                             uint32_t valid_bytes,
                             const bm_timestamp_t *timestamp);

/** 取消已 acquire 但未 commit 的生产（DMA_OWNED → FREE） */
int bm_stream_producer_abort(bm_stream_t *stream, bm_block_t *block);

int bm_stream_consumer_acquire(bm_stream_t *stream, bm_block_t **block);

int bm_stream_consumer_release(bm_stream_t *stream, bm_block_t *block);

int bm_stream_output_acquire(bm_stream_t *stream, bm_block_t **block);

int bm_stream_output_commit(bm_stream_t *stream,
                            bm_block_t *block,
                            uint32_t valid_bytes,
                            const bm_timestamp_t *timestamp);

/**
 * @brief 主循环 drain：可经 ready 回调通知
 *
 * @param stream 流实例
 * @param budget 本轮最多通知的 READY 块数
 * @return 实际调用 ready 回调的次数
 */
int bm_stream_drain(bm_stream_t *stream, uint32_t budget);

/* =========================================================================
 * bm_bus BLOCK 模式适配器
 * ========================================================================= */

/**
 * @brief 获取 bm_stream 的 bm_block_backend_iface_t vtable 指针
 *
 * 返回指向全局静态 vtable 的指针，供 bm_bus BLOCK 模式通过
 * bm_bus_bind_block_backend 绑定，以 bm_stream_t * 作为 ctx 传入。
 *
 * 用法示例：
 * @code
 * bm_bus_bind_block_backend(&h_block,
 *                            bm_stream_as_block_backend(),
 *                            &my_stream);
 * @endcode
 *
 * @note bm_stream 本体零改动；adapter 实现在 bm_stream_block_adapter.c。
 *
 * @return 指向全局静态 bm_block_backend_iface_t 的常量指针
 */
const bm_block_backend_iface_t *bm_stream_as_block_backend(void);

#endif /* BM_STREAM_H */

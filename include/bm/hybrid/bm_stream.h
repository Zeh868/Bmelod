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
 * 2026-07-27       1.2            zeh            将 BM_STREAM_* 静态分配宏迁到 bm_stream_impl.h
 * 2026-07-27       1.3            zeh            struct bm_stream 下沉到 .c，头文件改为不透明指针 + accessor
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

/* -------------------------------------------------------------------------
 * 内部字段 accessor（bm_stream 已改为不透明结构体）
 * ------------------------------------------------------------------------- */

bm_block_t *bm_stream_blocks(const bm_stream_t *stream);
uint32_t    bm_stream_block_count(const bm_stream_t *stream);
uint32_t    bm_stream_block_capacity(const bm_stream_t *stream);
bm_stream_policy_t bm_stream_policy_value(const bm_stream_t *stream);
const bm_stream_stats_t *bm_stream_stats(const bm_stream_t *stream);
bm_stream_ready_fn_t bm_stream_on_ready(const bm_stream_t *stream);
void       *bm_stream_on_ready_context(const bm_stream_t *stream);
int         bm_stream_initialized(const bm_stream_t *stream);
uint32_t    bm_stream_next_sequence(const bm_stream_t *stream);
uint8_t     bm_stream_owner_cpu(const bm_stream_t *stream);
uint8_t     bm_stream_pending_drain(const bm_stream_t *stream);

void bm_stream_set_blocks(bm_stream_t *stream, bm_block_t *blocks);
void bm_stream_set_block_count(bm_stream_t *stream, uint32_t count);
void bm_stream_set_block_capacity(bm_stream_t *stream, uint32_t cap);
void bm_stream_set_policy(bm_stream_t *stream, bm_stream_policy_t policy);
void bm_stream_set_ready_handler(bm_stream_t *stream,
                                 bm_stream_ready_fn_t handler,
                                 void *context);
void bm_stream_set_initialized(bm_stream_t *stream, int initialized);
void bm_stream_set_next_sequence(bm_stream_t *stream, uint32_t seq);
void bm_stream_set_owner_cpu(bm_stream_t *stream, uint8_t cpu);
void bm_stream_set_pending_drain(bm_stream_t *stream, uint8_t pending);

/**
 * @deprecated 下一轮发布删除，请改用 accessor API
 * 这些宏仅作为外部已发布用户的临时过渡。
 */
#define BM_STREAM_DEPRECATED_BLOCKS(s)         bm_stream_blocks(s)
#define BM_STREAM_DEPRECATED_BLOCK_COUNT(s)    bm_stream_block_count(s)
#define BM_STREAM_DEPRECATED_BLOCK_CAPACITY(s) bm_stream_block_capacity(s)
#define BM_STREAM_DEPRECATED_POLICY(s)         bm_stream_policy_value(s)
#define BM_STREAM_DEPRECATED_ON_READY(s)       bm_stream_on_ready(s)
#define BM_STREAM_DEPRECATED_ON_READY_CTX(s)   bm_stream_on_ready_context(s)
#define BM_STREAM_DEPRECATED_INITIALIZED(s)    bm_stream_initialized(s)
#define BM_STREAM_DEPRECATED_NEXT_SEQUENCE(s)  bm_stream_next_sequence(s)
#define BM_STREAM_DEPRECATED_OWNER_CPU(s)      bm_stream_owner_cpu(s)
#define BM_STREAM_DEPRECATED_PENDING_DRAIN(s)  bm_stream_pending_drain(s)

void bm_stream_mark_late(bm_stream_t *stream);

int bm_stream_init(bm_stream_t *stream,
                   void *payloads,
                   uint32_t block_count,
                   uint32_t block_bytes);

void bm_stream_reset(bm_stream_t *stream);

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

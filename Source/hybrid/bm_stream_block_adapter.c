/**
 * @file bm_stream_block_adapter.c
 * @brief bm_stream -> bm_block_backend_iface_t 适配器
 *
 * 将 bm_stream 的 producer/consumer 系列 API 包装成 bm_block_backend_iface_t
 * 函数指针表（vtable），供 bm_bus BLOCK 模式经 bm_bus_bind_block_backend 绑定使用。
 *
 * 依赖方向：adapter（hybrid） -> bm_stream（hybrid） -> bm_block_backend（core）。
 * 反向解耦（core 不引用 hybrid 类型）通过不透明 void * 实现。
 *
 * bm_block_t * 与 void * 的互转约定：
 *   - producer_acquire / consumer_acquire：以 (void *) 形式输出 bm_block_t * 指针；
 *   - producer_commit / producer_abort / consumer_release：从 void * 还原 bm_block_t *。
 *
 * ts_ns（uint64_t 纳秒）-> bm_timestamp_t 的转换策略：
 *   若 ts_ns == 0，传 NULL（bm_stream_producer_commit 允许空时间戳）；
 *   否则填充 ticks = (uint32_t)(ts_ns / 1000u)、rate_hz = 1000000u（微秒粒度）。
 *   真实平台可在更高层构造 bm_timestamp_t 并绕过 bus 层。
 *
 * @note bm_stream 核心逻辑保持不变；本文件仅新增一层薄适配。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            初稿：bm_stream 适配为 bm_block_backend_iface_t
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/hybrid/bm_stream.h"
#include "bm/core/bm_block_backend.h"

/* ------------------------------------------------------------------ */
/* 静态适配函数（统一以 adapter_ 前缀命名）                            */
/* ------------------------------------------------------------------ */

/**
 * @brief 将 uint64_t 纳秒时间戳转换为 bm_timestamp_t（微秒粒度）
 *
 * 入参为 0 表示"无时间戳"，此时调用方应改传 NULL；
 * 非零时按 1 MHz（微秒粒度）填充 ticks，clock_id=0（HRT 时钟域）。
 *
 * @param ts_ns 纳秒时间戳（0 表示无效）
 * @param out   输出：bm_timestamp_t
 */
static void adapter_ts_from_ns(uint64_t ts_ns, bm_timestamp_t *out) {
    out->clock_id    = 0u;
    out->quality     = 0u;
    out->clock_epoch = 0u;
    out->ticks       = (uint32_t)(ts_ns / 1000u); /* 纳秒 -> 微秒 */
    out->rate_hz     = 1000000u;                  /* 1 MHz，对应微秒粒度 */
}

/**
 * @brief 适配器：生产者借块（将 bm_block_t * 包装为 void *）
 *
 * @param ctx       后端上下文（bm_stream_t *）
 * @param block_out 输出：(void *) 形式的 bm_block_t * 指针
 * @return bm_stream_producer_acquire 的返回码
 */
static int adapter_producer_acquire(void *ctx, void **block_out) {
    bm_stream_t *stream = (bm_stream_t *)ctx;
    bm_block_t  *block  = NULL;
    int          rc;

    if (!stream || !block_out) {
        return BM_ERR_INVALID;
    }
    rc = bm_stream_producer_acquire(stream, &block);
    *block_out = (void *)block;
    return rc;
}

/**
 * @brief 适配器：生产者提交块（从 void * 还原 bm_block_t *）
 *
 * @param ctx         后端上下文（bm_stream_t *）
 * @param block       (void *) 形式的 bm_block_t * 块指针
 * @param valid_bytes 有效数据字节数
 * @param ts_ns       时间戳（纳秒），0 表示无时间戳
 * @return bm_stream_producer_commit 的返回码
 */
static int adapter_producer_commit(void *ctx, void *block,
                                   uint32_t valid_bytes, uint64_t ts_ns) {
    bm_stream_t    *stream    = (bm_stream_t *)ctx;
    bm_block_t     *blk       = (bm_block_t *)block;
    bm_timestamp_t  ts_val;
    const bm_timestamp_t *ts_ptr;

    if (!stream || !blk) {
        return BM_ERR_INVALID;
    }
    if (ts_ns == 0u) {
        ts_ptr = NULL;
    } else {
        adapter_ts_from_ns(ts_ns, &ts_val);
        ts_ptr = &ts_val;
    }
    return bm_stream_producer_commit(stream, blk, valid_bytes, ts_ptr);
}

/**
 * @brief 适配器：生产者放弃已借块（从 void * 还原 bm_block_t *）
 *
 * @param ctx   后端上下文（bm_stream_t *）
 * @param block (void *) 形式的 bm_block_t * 块指针
 * @return bm_stream_producer_abort 的返回码
 */
static int adapter_producer_abort(void *ctx, void *block) {
    bm_stream_t *stream = (bm_stream_t *)ctx;
    bm_block_t  *blk    = (bm_block_t *)block;

    if (!stream || !blk) {
        return BM_ERR_INVALID;
    }
    return bm_stream_producer_abort(stream, blk);
}

/**
 * @brief 适配器：消费者借块（将 bm_block_t * 包装为 void *）
 *
 * @param ctx       后端上下文（bm_stream_t *）
 * @param block_out 输出：(void *) 形式的 bm_block_t * 指针
 * @return bm_stream_consumer_acquire 的返回码
 */
static int adapter_consumer_acquire(void *ctx, void **block_out) {
    bm_stream_t *stream = (bm_stream_t *)ctx;
    bm_block_t  *block  = NULL;
    int          rc;

    if (!stream || !block_out) {
        return BM_ERR_INVALID;
    }
    rc = bm_stream_consumer_acquire(stream, &block);
    *block_out = (void *)block;
    return rc;
}

/**
 * @brief 适配器：消费者归还块（从 void * 还原 bm_block_t *）
 *
 * @param ctx   后端上下文（bm_stream_t *）
 * @param block (void *) 形式的 bm_block_t * 块指针
 * @return bm_stream_consumer_release 的返回码
 */
static int adapter_consumer_release(void *ctx, void *block) {
    bm_stream_t *stream = (bm_stream_t *)ctx;
    bm_block_t  *blk    = (bm_block_t *)block;

    if (!stream || !blk) {
        return BM_ERR_INVALID;
    }
    return bm_stream_consumer_release(stream, blk);
}

/* ------------------------------------------------------------------ */
/* 静态 vtable 实例（无状态；多个 bus 实例可共享同一 iface 指针）      */
/* ------------------------------------------------------------------ */

/** @brief bm_stream 适配器 vtable（全局单例，只读） */
static const bm_block_backend_iface_t s_stream_backend_iface = {
    adapter_producer_acquire,
    adapter_producer_commit,
    adapter_producer_abort,
    adapter_consumer_acquire,
    adapter_consumer_release,
};

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief 获取 bm_stream 适配器 vtable 指针
 *
 * 返回静态 vtable 指针，供 bm_bus_bind_block_backend 绑定使用，
 * 并以 bm_stream_t * 作为 ctx 传入。
 *
 * 用法示例：
 * @code
 * bm_bus_bind_block_backend(&h_block,
 *                            bm_stream_as_block_backend(),
 *                            &my_stream);
 * @endcode
 *
 * @return 指向全局静态 bm_block_backend_iface_t 的指针（生命周期 = 进程生命周期）
 */
const bm_block_backend_iface_t *bm_stream_as_block_backend(void) {
    return &s_stream_backend_iface;
}

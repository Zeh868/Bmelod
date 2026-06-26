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
 * ts_ns（uint64_t 纳秒）-> bm_timestamp_t 的转换策略（#9-1c 统一时间基）：
 *   - ts_ns != 0：纳秒原值直存 ticks、rate_hz = 1000000000u（1 GHz，ns 粒度），
 *     零截断、零精度损失；
 *   - ts_ns == 0：用统一单调时钟 bm_uptime_ns() 自动打戳（不再留空/NULL）；
 *   - clock_id 反映 stream 的 owner_cpu（bm_timestamp_clock_for_cpu）。
 *   消费端按通用公式 `ticks * 1e6 / rate_hz` 换算微秒，与粒度无关。
 *   真实平台仍可在更高层构造 bm_timestamp_t 并绕过 bus 层。
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
 * @brief 构造 ns 粒度 bm_timestamp_t（#9-1c 统一时间基，清偿精度债）
 *
 * - ts_ns != 0：纳秒原值直存 ticks，rate_hz = 1e9（ns 粒度），零截断；
 * - ts_ns == 0：用统一单调时钟 bm_uptime_ns() 自动打戳兜底；
 * - clock_id 反映 owner_cpu（HRT 时钟域按核区分）。
 *
 * @param ts_ns     纳秒时间戳（0 表示调用方未给，触发 uptime 兜底）
 * @param owner_cpu stream 的 owner CPU，用于填充 clock_id
 * @param out       输出：bm_timestamp_t
 */
static void adapter_ts_from_ns(uint64_t ts_ns, uint8_t owner_cpu,
                               bm_timestamp_t *out) {
    if (ts_ns == 0u) {
        /* 无显式时间戳：统一单调时钟兜底（ns 直存，rate_hz=1e9） */
        *out = bm_timestamp_from_uptime();
    } else {
        out->quality     = 0u;
        out->clock_epoch = 0u;
        out->ticks       = ts_ns;          /* 纳秒原值直存，零截断 */
        out->rate_hz     = 1000000000u;    /* 1 GHz，对应纳秒粒度 */
    }
    out->clock_id = bm_timestamp_clock_for_cpu((uint32_t)owner_cpu);
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
 * @param ts_ns       时间戳（纳秒），0 表示由统一时钟自动打戳
 * @return bm_stream_producer_commit 的返回码
 */
static int adapter_producer_commit(void *ctx, void *block,
                                   uint32_t valid_bytes, uint64_t ts_ns) {
    bm_stream_t    *stream    = (bm_stream_t *)ctx;
    bm_block_t     *blk       = (bm_block_t *)block;
    bm_timestamp_t  ts_val;

    if (!stream || !blk) {
        return BM_ERR_INVALID;
    }
    /* #9-1c：始终携带时间戳——显式 ns 直存，缺省时 bm_uptime_ns() 兜底 */
    adapter_ts_from_ns(ts_ns, stream->owner_cpu, &ts_val);
    return bm_stream_producer_commit(stream, blk, valid_bytes, &ts_val);
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

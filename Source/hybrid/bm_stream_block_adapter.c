/**
 * @file bm_stream_block_adapter.c
 * @brief bm_stream -> bm_block_backend_iface_t adapter
 *
 * bm_stream producer_xxx/consumer_xxx API wrapper as bm_block_backend_iface_t vtable,
 * for bm_bus BLOCK mode via bm_bus_bind_block_backend.
 *
 * Dependency: adapter (hybrid) -> bm_stream (hybrid) -> bm_block_backend (core).
 * Reverse (core not referencing hybrid) is achieved via opaque void *.
 *
 * bm_block_t * <-> void * conversion:
 *   - producer_acquire / consumer_acquire: return (void *)bm_block_t*
 *   - producer_commit / producer_abort / consumer_release: receive (bm_block_t *)(void *)
 *
 * ts_ns (uint64_t ns) -> bm_timestamp_t conversion strategy:
 *   If ts_ns == 0, pass NULL (bm_stream_producer_commit allows NULL timestamp).
 *   Otherwise fill ticks = (uint32_t)(ts_ns / 1000u), rate_hz = 1000000u (us granularity).
 *   Real platforms may construct bm_timestamp_t at a higher level and bypass bus layer.
 *
 * @note bm_stream core logic is unchanged; this file only adds a thin adapter layer.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par Revision History:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            bm_stream adapter for bm_block_backend_iface_t
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/hybrid/bm_stream.h"
#include "bm/core/bm_block_backend.h"

/* ------------------------------------------------------------------ */
/* static adapter functions (named with adapter_ prefix)              */
/* ------------------------------------------------------------------ */

/**
 * @brief Convert uint64_t nanosecond timestamp to bm_timestamp_t (microsecond granularity)
 *
 * 0 means "no timestamp"; callers should pass NULL in that case.
 * Non-zero: fill ticks at 1 MHz (us granularity), clock_id=0 (HRT domain).
 *
 * @param ts_ns nanosecond timestamp (0=invalid)
 * @param out   output bm_timestamp_t
 */
static void adapter_ts_from_ns(uint64_t ts_ns, bm_timestamp_t *out) {
    out->clock_id    = 0u;
    out->quality     = 0u;
    out->clock_epoch = 0u;
    out->ticks       = (uint32_t)(ts_ns / 1000u); /* ns -> us */
    out->rate_hz     = 1000000u;                  /* 1 MHz, matches us granularity */
}

/**
 * @brief adapter: producer_acquire (wrap bm_block_t * as void *)
 *
 * @param ctx       bm_stream_t * context
 * @param block_out output: (void *)bm_block_t*
 * @return bm_stream_producer_acquire return code
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
 * @brief adapter: producer_commit (restore bm_block_t * from void *)
 *
 * @param ctx         bm_stream_t * context
 * @param block       (bm_block_t *)(void *) block pointer
 * @param valid_bytes valid data byte count
 * @param ts_ns       timestamp (nanoseconds), 0 = no timestamp
 * @return bm_stream_producer_commit return code
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
 * @brief adapter: producer_abort (restore bm_block_t * from void *)
 *
 * @param ctx   bm_stream_t * context
 * @param block (bm_block_t *)(void *) block pointer
 * @return bm_stream_producer_abort return code
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
 * @brief adapter: consumer_acquire (wrap bm_block_t * as void *)
 *
 * @param ctx       bm_stream_t * context
 * @param block_out output: (void *)bm_block_t*
 * @return bm_stream_consumer_acquire return code
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
 * @brief adapter: consumer_release (restore bm_block_t * from void *)
 *
 * @param ctx   bm_stream_t * context
 * @param block (bm_block_t *)(void *) block pointer
 * @return bm_stream_consumer_release return code
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
/* Static vtable instance (stateless; multiple bus instances may      */
/* share the same iface pointer)                                      */
/* ------------------------------------------------------------------ */

/** @brief bm_stream adapter vtable (global singleton, read-only) */
static const bm_block_backend_iface_t s_stream_backend_iface = {
    adapter_producer_acquire,
    adapter_producer_commit,
    adapter_producer_abort,
    adapter_consumer_acquire,
    adapter_consumer_release,
};

/* ------------------------------------------------------------------ */
/* Public interface                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Get the bm_stream adapter vtable pointer
 *
 * Returns a pointer to the static vtable for use with bm_bus_bind_block_backend,
 * passing a bm_stream_t * as ctx.
 *
 * Usage example:
 * @code
 * bm_bus_bind_block_backend(&h_block,
 *                            bm_stream_as_block_backend(),
 *                            &my_stream);
 * @endcode
 *
 * @return Pointer to global static bm_block_backend_iface_t (lifetime = process lifetime)
 */
const bm_block_backend_iface_t *bm_stream_as_block_backend(void) {
    return &s_stream_backend_iface;
}

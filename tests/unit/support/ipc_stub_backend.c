/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ipc_stub_backend.c
 * @brief BM_BUS_IPC native 测试桩：复刻 bm_ipc 的拷入拷出 + CRC + seqlock 语义。
 */
#include "ipc_stub_backend.h"
#include "bm/common/bm_crc32.h"
#include "bm/common/bm_types.h"
#include <string.h>

void fifo_stub_init(fifo_stub_t *s, uint32_t elem, uint32_t cap) {
    memset(s, 0, sizeof(*s));
    s->elem = elem;
    s->cap  = cap;
}

/** @brief FIFO acquire_write：满则拒，否则借出暂存。 */
static int fifo_acq_w(void *ctx, void **slot_out) {
    fifo_stub_t *s = (fifo_stub_t *)ctx;
    uint32_t head = bm_atomic_ipc_load_u32(&s->head);
    uint32_t tail = bm_atomic_ipc_load_u32(&s->tail);
    if (s->wr_pending) { return BM_ERR_BUSY; }
    if ((head - tail) >= (s->cap - 1u)) { return BM_ERR_OVERFLOW; }
    s->wr_pending = 1u;
    *slot_out = s->scratch;
    return BM_OK;
}
/** @brief FIFO commit：暂存→共享槽 + CRC，发布后推进 head。 */
static int fifo_commit(void *ctx) {
    fifo_stub_t *s = (fifo_stub_t *)ctx;
    uint32_t head = bm_atomic_ipc_load_u32(&s->head);
    uint32_t idx  = head % s->cap;
    if (!s->wr_pending) { return BM_ERR_INVALID; }
    memcpy(s->shared[idx], s->scratch, s->elem);
    s->crc[idx] = bm_crc32(s->shared[idx], s->elem);
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&s->head, head + 1u);
    s->wr_pending = 0u;
    return BM_OK;
}
static int fifo_abort(void *ctx) {
    fifo_stub_t *s = (fifo_stub_t *)ctx;
    s->wr_pending = 0u;
    return BM_OK;
}
/** @brief FIFO acquire_read：空则阻塞，否则共享槽→暂存 + 校验 CRC。 */
static int fifo_acq_r(void *ctx, const void **slot_out) {
    fifo_stub_t *s = (fifo_stub_t *)ctx;
    uint32_t head = bm_atomic_ipc_load_u32(&s->head);
    uint32_t tail = bm_atomic_ipc_load_u32(&s->tail);
    uint32_t idx  = tail % s->cap;
    if (head == tail) { return BM_ERR_WOULD_BLOCK; }
    bm_atomic_ipc_fence_acquire();
    memcpy(s->scratch, s->shared[idx], s->elem);
    if (bm_crc32(s->scratch, s->elem) != s->crc[idx]) { return BM_ERR_INVALID; }
    *slot_out = s->scratch;
    return BM_OK;
}
static int fifo_release(void *ctx) {
    fifo_stub_t *s = (fifo_stub_t *)ctx;
    uint32_t tail = bm_atomic_ipc_load_u32(&s->tail);
    bm_atomic_ipc_store_u32(&s->tail, tail + 1u);
    return BM_OK;
}
const bm_ipc_backend_iface_t g_fifo_ipc_iface = {
    fifo_acq_w, fifo_commit, fifo_abort, fifo_acq_r, fifo_release
};

void latest_stub_init(latest_stub_t *s, uint32_t elem) {
    memset(s, 0, sizeof(*s));
    s->elem = elem;
}
static int lat_acq_w(void *ctx, void **slot_out) {
    latest_stub_t *s = (latest_stub_t *)ctx;
    if (s->wr_pending) { return BM_ERR_BUSY; }
    s->wr_pending = 1u;
    *slot_out = s->scratch_w;
    return BM_OK;
}
/** @brief LATEST commit：seqlock 奇→拷贝+CRC→偶，置 published。 */
static int lat_commit(void *ctx) {
    latest_stub_t *s = (latest_stub_t *)ctx;
    uint32_t seq = bm_atomic_ipc_load_u32(&s->seq);
    if (!s->wr_pending) { return BM_ERR_INVALID; }
    bm_atomic_ipc_store_u32(&s->seq, seq + 1u);        /* 奇：写进行中 */
    memcpy(s->shared, s->scratch_w, s->elem);
    s->crc = bm_crc32(s->shared, s->elem);
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&s->seq, seq + 2u);        /* 偶：稳定 */
    s->published = 1u;
    s->wr_pending = 0u;
    return BM_OK;
}
static int lat_abort(void *ctx) {
    latest_stub_t *s = (latest_stub_t *)ctx;
    s->wr_pending = 0u;
    return BM_OK;
}
/** @brief LATEST acquire_read：未发布阻塞；seqlock 拷出 + CRC 校验。 */
static int lat_acq_r(void *ctx, const void **slot_out) {
    latest_stub_t *s = (latest_stub_t *)ctx;
    uint32_t s1, s2;
    if (!s->published) { return BM_ERR_WOULD_BLOCK; }
    s1 = bm_atomic_ipc_load_u32(&s->seq);
    if ((s1 & 1u) != 0u) { return BM_ERR_WOULD_BLOCK; }
    memcpy(s->scratch_r, s->shared, s->elem);
    bm_atomic_ipc_fence_acquire();
    s2 = bm_atomic_ipc_load_u32(&s->seq);
    if (s2 != s1) { return BM_ERR_WOULD_BLOCK; }
    if (bm_crc32(s->scratch_r, s->elem) != s->crc) { return BM_ERR_INVALID; }
    *slot_out = s->scratch_r;
    return BM_OK;
}
static int lat_release(void *ctx) { (void)ctx; return BM_OK; }
const bm_ipc_backend_iface_t g_latest_ipc_iface = {
    lat_acq_w, lat_commit, lat_abort, lat_acq_r, lat_release
};

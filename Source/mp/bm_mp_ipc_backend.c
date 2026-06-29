/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_mp_ipc_backend.c
 * @brief matrix payload 通道 adapter 实现（FIFO + LATEST）。
 *
 * adapter 自包含——仅操作 bm_mp_ipc_matrix_t 的 cmd_ring/tel_channel 字段，
 * 不引用 matrix.c 的多核 HAL（cpu/boot/drain/critical/cache）。
 * 热路径：定长 memcpy（ctx->elem 字节），无动态分配；
 * LATEST 读失稳即返回，不自旋（WCET 可静态分析）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-27       1.0            zeh            阶段 2：FIFO(cmd_ring) + LATEST(tel_channel) adapter
 *
 */
#include "bm/mp/bm_mp_ipc_backend.h"
#include "bm/common/bm_crc32.h"
#include "bm/common/bm_types.h"
#include <string.h>

/* payload size 兜底静态断言 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE >= 1u,
               "BM_CONFIG_MP_IPC_CMD_PAYLOAD_SIZE must be at least 1");
_Static_assert(BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE >= 1u,
               "BM_CONFIG_MP_IPC_TEL_PAYLOAD_SIZE must be at least 1");
#endif

/* ------------------------------------------------------------------ */
/* open                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化 adapter ctx，绑定 matrix 的 (source→target) 通道。
 *
 * @param ctx    adapter 上下文
 * @param m      共享矩阵
 * @param source 源核（须 < BM_CONFIG_CPU_COUNT）
 * @param target 目标核（须 < BM_CONFIG_CPU_COUNT）
 * @param elem   payload 字节数（须 > 0）
 * @return BM_OK；BM_ERR_INVALID（参数非法）
 */
int bm_mp_ipc_backend_open(bm_mp_ipc_backend_ctx_t *ctx, bm_mp_ipc_matrix_t *m,
                            uint8_t source, uint8_t target, uint32_t elem)
{
    if (!ctx || !m ||
        source >= (uint8_t)BM_CONFIG_CPU_COUNT ||
        target >= (uint8_t)BM_CONFIG_CPU_COUNT ||
        elem == 0u) {
        return BM_ERR_INVALID;
    }
    if (elem > sizeof(ctx->scratch)) { return BM_ERR_INVALID; }
    memset(ctx, 0, sizeof(*ctx));
    ctx->m      = m;
    ctx->source = source;
    ctx->target = target;
    ctx->elem   = elem;
    return BM_OK;
}

/* ================================================================== */
/* FIFO 后端（cmd_ring[source][target]）                               */
/* SPSC head/tail+fence；满判 DEPTH-1；无每槽 CRC（速度优先）         */
/*                                                                    */
/* [F-5 WCET 边界] 热路径各函数满足 bm_ipc_backend_iface_t 确定性契约：*/
/*   acquire_write : O(1)——2 次原子 load + 标记写，满时立即返回 ERR   */
/*   commit        : O(payload)——memcpy(ctx->elem) + release fence +  */
/*                   原子 store；ctx->elem ≤ sizeof(payload)，编译期界 */
/*   abort         : O(1)——仅清 wr_pending 标记                        */
/*   acquire_read  : O(payload)——满/空判断 + acquire fence +           */
/*                   memcpy(ctx->elem)；空时立即返回 WOULD_BLOCK        */
/*   release       : O(1)——原子 store 推进 tail 游标                   */
/* ================================================================== */

/**
 * @brief FIFO acquire_write：满则拒，否则借出写暂存 ctx->scratch。
 *
 * @param c       bm_mp_ipc_backend_ctx_t*
 * @param slot_out 输出写暂存指针
 * @return BM_OK；BM_ERR_BUSY（重入）；BM_ERR_OVERFLOW（环满）
 */
static int fifo_acq_w(void *c, void **slot_out)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;
    bm_mp_ipc_cmd_ring_t    *r   = &ctx->m->cmd_ring[ctx->source][ctx->target];
    uint32_t head = bm_atomic_ipc_load_u32(&r->head.value);
    uint32_t tail = bm_atomic_ipc_load_u32(&r->tail.value);

    if (ctx->wr_pending) { return BM_ERR_BUSY; }
    if ((head - tail) >= (BM_CONFIG_MP_IPC_CMD_RING_DEPTH - 1u)) {
        return BM_ERR_OVERFLOW;
    }
    ctx->wr_pending = 1u;
    *slot_out = ctx->scratch;
    return BM_OK;
}

/**
 * @brief FIFO commit：写暂存→共享槽 + release fence，推进 head 游标。
 *
 * @param c bm_mp_ipc_backend_ctx_t*
 * @return BM_OK；BM_ERR_INVALID（无未决写）
 */
static int fifo_commit(void *c)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;
    bm_mp_ipc_cmd_ring_t    *r   = &ctx->m->cmd_ring[ctx->source][ctx->target];
    uint32_t head = bm_atomic_ipc_load_u32(&r->head.value);
    uint32_t idx  = head % BM_CONFIG_MP_IPC_CMD_RING_DEPTH;

    if (!ctx->wr_pending) { return BM_ERR_INVALID; }
    memcpy(r->slots[idx], ctx->scratch, ctx->elem);
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&r->head.value, head + 1u);
    ctx->wr_pending = 0u;
    return BM_OK;
}

/**
 * @brief FIFO abort：清写暂存标记，不发布。
 *
 * @param c bm_mp_ipc_backend_ctx_t*
 * @return BM_OK
 */
static int fifo_abort(void *c)
{
    ((bm_mp_ipc_backend_ctx_t *)c)->wr_pending = 0u;
    return BM_OK;
}

/**
 * @brief FIFO acquire_read：空则阻塞，否则共享槽→读暂存 + acquire fence。
 *
 * @param c        bm_mp_ipc_backend_ctx_t*
 * @param slot_out 输出只读暂存指针
 * @return BM_OK；BM_ERR_WOULD_BLOCK（空）
 */
static int fifo_acq_r(void *c, const void **slot_out)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;
    bm_mp_ipc_cmd_ring_t    *r   = &ctx->m->cmd_ring[ctx->source][ctx->target];
    uint32_t head = bm_atomic_ipc_load_u32(&r->head.value);
    uint32_t tail = bm_atomic_ipc_load_u32(&r->tail.value);
    uint32_t idx  = tail % BM_CONFIG_MP_IPC_CMD_RING_DEPTH;

    if (head == tail) { return BM_ERR_WOULD_BLOCK; }
    bm_atomic_ipc_fence_acquire();
    memcpy(ctx->scratch_r, r->slots[idx], ctx->elem);
    *slot_out = ctx->scratch_r;
    return BM_OK;
}

/**
 * @brief FIFO release：推进 tail 游标，归还已读槽。
 *
 * @param c bm_mp_ipc_backend_ctx_t*
 * @return BM_OK
 */
static int fifo_release(void *c)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;
    bm_mp_ipc_cmd_ring_t    *r   = &ctx->m->cmd_ring[ctx->source][ctx->target];
    uint32_t tail = bm_atomic_ipc_load_u32(&r->tail.value);

    bm_atomic_ipc_store_u32(&r->tail.value, tail + 1u);
    return BM_OK;
}

/** @brief FIFO 后端 vtable（映射 cmd_ring）。 */
const bm_ipc_backend_iface_t g_mp_ipc_fifo_iface = {
    fifo_acq_w, fifo_commit, fifo_abort, fifo_acq_r, fifo_release
};

/* ================================================================== */
/* LATEST 后端（tel_channel[source][target]）                          */
/* seqlock：奇=写进行中，偶=稳定；CRC 校验；读失稳即返回，不自旋      */
/*                                                                    */
/* [F-5 WCET 边界] 热路径各函数满足 bm_ipc_backend_iface_t 确定性契约：*/
/*   acquire_write : O(1)——仅设 wr_pending 标记，重入立即返回 BUSY     */
/*   commit        : O(payload)——seqlock(奇) + memcpy + CRC32 +       */
/*                   release fence + seqlock(偶)；payload 为编译期界   */
/*                   CRC32 逐字节：T_crc ≈ 8 cycle/byte × payload_len  */
/*   abort         : O(1)——仅清 wr_pending 标记                        */
/*   acquire_read  : O(payload)——seq 奇/未发布立即返回 WOULD_BLOCK；   */
/*                   稳定时 memcpy + acquire fence + seq 复验 + CRC 验  */
/*                   seqlock 失稳立即返回 WOULD_BLOCK，禁止自旋重试    */
/*   release       : O(1)——幂等空操作（LATEST 无读游标）               */
/* ================================================================== */

/**
 * @brief LATEST acquire_write：借出写暂存 ctx->scratch。
 *
 * @param c        bm_mp_ipc_backend_ctx_t*
 * @param slot_out 输出写暂存指针
 * @return BM_OK；BM_ERR_BUSY（重入）
 */
static int lat_acq_w(void *c, void **slot_out)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;

    if (ctx->wr_pending) { return BM_ERR_BUSY; }
    ctx->wr_pending = 1u;
    *slot_out = ctx->scratch;
    return BM_OK;
}

/**
 * @brief LATEST commit：seqlock 奇→拷贝+CRC→偶，发布最新值。
 *
 * 复刻 bm_ipc write_channel 语义：
 *   seq 奇=写进行中 → memcpy + CRC → release fence → seq 偶=稳定。
 *
 * @param c bm_mp_ipc_backend_ctx_t*
 * @return BM_OK；BM_ERR_INVALID（无未决写）
 */
static int lat_commit(void *c)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;
    bm_mp_ipc_tel_channel_t *t   = &ctx->m->tel_channel[ctx->source][ctx->target];
    uint32_t prev = bm_atomic_ipc_load_u32(&t->seq);
    uint32_t next = prev + 1u;

    if (!ctx->wr_pending) { return BM_ERR_INVALID; }
    /* 保证 next 为奇（写进行中）：若 prev+1 已为偶则再加 1 */
    if ((next & 1u) == 0u) { next++; }
    bm_atomic_ipc_store_u32(&t->seq, next);          /* 奇：写进行中 */
    memcpy(t->payload, ctx->scratch, ctx->elem);
    t->crc32 = bm_crc32(t->payload, ctx->elem);
    bm_atomic_ipc_fence_release();
    bm_atomic_ipc_store_u32(&t->seq, next + 1u);     /* 偶：稳定 */
    ctx->wr_pending = 0u;
    return BM_OK;
}

/**
 * @brief LATEST abort：清写暂存标记，不发布。
 *
 * @param c bm_mp_ipc_backend_ctx_t*
 * @return BM_OK
 */
static int lat_abort(void *c)
{
    ((bm_mp_ipc_backend_ctx_t *)c)->wr_pending = 0u;
    return BM_OK;
}

/**
 * @brief LATEST acquire_read：seqlock 拷出 + CRC 校验；可重复读最新已发布值。
 *
 * 遥测"最新值通道"语义：只要 seq 为偶（稳定）且非 0（已发布），
 * 均可读出最新值，支持同帧重复读（无 last_seq 去重）。
 * 失稳（seq 奇或窗口内变化）即返回 WOULD_BLOCK，不自旋，WCET 有界（DET-01）。
 *
 * @param c        bm_mp_ipc_backend_ctx_t*
 * @param slot_out 输出只读读暂存指针
 * @return BM_OK；BM_ERR_WOULD_BLOCK（未发布/写进行中/失稳）；BM_ERR_INVALID（CRC 失配）
 */
static int lat_acq_r(void *c, const void **slot_out)
{
    bm_mp_ipc_backend_ctx_t *ctx = (bm_mp_ipc_backend_ctx_t *)c;
    bm_mp_ipc_tel_channel_t *t   = &ctx->m->tel_channel[ctx->source][ctx->target];
    uint32_t s   = bm_atomic_ipc_load_u32(&t->seq);
    uint32_t crc;

    if (s == 0u || (s & 1u) != 0u) { return BM_ERR_WOULD_BLOCK; } /* 未发布/写进行中 */
    memcpy(ctx->scratch_r, t->payload, ctx->elem);
    crc = bm_crc32(ctx->scratch_r, ctx->elem);
    bm_atomic_ipc_fence_acquire();
    /* seqlock 验证：若写者在拷贝窗口内发布新值，seq 已变，失稳即返回，不自旋 */
    if (bm_atomic_ipc_load_u32(&t->seq) != s) { return BM_ERR_WOULD_BLOCK; }
    if (crc != t->crc32)                       { return BM_ERR_INVALID; }
    *slot_out = ctx->scratch_r;
    return BM_OK;
}

/**
 * @brief LATEST release：幂等（LATEST 后端不持读游标）。
 *
 * @param c bm_mp_ipc_backend_ctx_t*
 * @return BM_OK
 */
static int lat_release(void *c)
{
    (void)c;
    return BM_OK;
}

/** @brief LATEST 后端 vtable（映射 tel_channel）。 */
const bm_ipc_backend_iface_t g_mp_ipc_latest_iface = {
    lat_acq_w, lat_commit, lat_abort, lat_acq_r, lat_release
};

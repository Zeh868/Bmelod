/**
 * @file bm_block.h
 * @brief 流式数据块描述符（零拷贝 payload 指针）
 *
 * bm_block_t 描述静态缓冲区内一块 payload 的容量、有效长度、序号、时间戳与
 * 所有权状态，供 bm_stream 与 DMA HAL 在生产者/消费者之间交接。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            acquire/release 原子状态字
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_BLOCK_H
#define BM_BLOCK_H

#include "bm/hybrid/bm_timestamp.h"
#include "bm/common/bm_atomic_ipc.h"

#include <stdint.h>

typedef enum {
    BM_BLOCK_STATE_FREE = 0,
    BM_BLOCK_STATE_DMA_OWNED,
    BM_BLOCK_STATE_READY,
    BM_BLOCK_STATE_PROCESSING,
    BM_BLOCK_STATE_OUTPUT_READY
} bm_block_state_t;

typedef struct {
    void              *data;
    uint32_t           capacity_bytes;
    uint32_t           valid_bytes;
    uint32_t           sequence;
    bm_timestamp_t     timestamp;
    uint16_t           format;
    uint16_t           flags;
    bm_atomic_ipc_u32_t state;
} bm_block_t;

static inline bm_block_state_t bm_block_state_load(const bm_block_t *block) {
    if (!block) {
        return BM_BLOCK_STATE_FREE;
    }
    return (bm_block_state_t)bm_atomic_ipc_load_u32(&block->state);
}

static inline void bm_block_state_store(bm_block_t *block,
                                        bm_block_state_t state) {
    if (block) {
        bm_atomic_ipc_store_u32(&block->state, (uint32_t)state);
    }
}

#endif /* BM_BLOCK_H */

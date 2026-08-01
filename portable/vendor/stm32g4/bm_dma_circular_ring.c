/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_dma_circular_ring.c
 * @brief DMA 循环模式软件读指针记账实现
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-29
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-29       1.0            zeh            新增 DMA 循环模式环形缓冲记账模块
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_dma_circular_ring.h"

void bm_dma_circ_ring_reset(bm_dma_circ_ring_t *ring,
                            uint8_t *buf, uint32_t len) {
    if (ring == NULL) {
        return;
    }
    ring->buf = buf;
    ring->len = (len > 0u) ? len : 1u;
    ring->produced = 0u;
    ring->consumed = 0u;
}

uint32_t bm_dma_circ_ring_update(bm_dma_circ_ring_t *ring, uint32_t ndtr) {
    uint32_t completed_cycles;
    uint32_t base;
    uint32_t pos_in_cycle;
    uint32_t new_produced;
    uint32_t new_bytes;

    if (ring == NULL || ring->len == 0u) {
        return 0u;
    }
    if (ndtr > ring->len) {
        ndtr = ring->len;
    }

    completed_cycles = ring->produced / ring->len;
    base = completed_cycles * ring->len;
    pos_in_cycle = ring->len - ndtr;
    new_produced = base + pos_in_cycle;

    if (new_produced < ring->produced) {
        /* 32-bit 溢出保护：不应发生，若发生则保持原值 */
        return 0u;
    }
    new_bytes = new_produced - ring->produced;
    ring->produced = new_produced;
    return new_bytes;
}

void bm_dma_circ_ring_mark_full_round(bm_dma_circ_ring_t *ring) {
    uint32_t completed_cycles;
    uint32_t next_boundary;

    if (ring == NULL || ring->len == 0u) {
        return;
    }
    completed_cycles = ring->produced / ring->len;
    next_boundary = (completed_cycles + 1u) * ring->len;
    ring->produced = next_boundary;
}

uint32_t bm_dma_circ_ring_pending(const bm_dma_circ_ring_t *ring) {
    if (ring == NULL) {
        return 0u;
    }
    return ring->produced - ring->consumed;
}

uint32_t bm_dma_circ_ring_free_space(const bm_dma_circ_ring_t *ring) {
    if (ring == NULL || ring->len == 0u) {
        return 0u;
    }
    return ring->len - (ring->produced - ring->consumed);
}

void bm_dma_circ_ring_drop_all(bm_dma_circ_ring_t *ring) {
    if (ring == NULL) {
        return;
    }
    ring->consumed = ring->produced;
}

uint32_t bm_dma_circ_ring_consume(bm_dma_circ_ring_t *ring,
                                  uint8_t *dst, uint32_t max_len) {
    uint32_t pending;
    uint32_t n;
    uint32_t i;

    if (ring == NULL || ring->buf == NULL || dst == NULL || max_len == 0u) {
        return 0u;
    }
    pending = ring->produced - ring->consumed;
    n = (pending < max_len) ? pending : max_len;
    for (i = 0u; i < n; ++i) {
        dst[i] = ring->buf[ring->consumed % ring->len];
        ring->consumed++;
    }
    return n;
}

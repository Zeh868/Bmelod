/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_dma_circular_ring.h
 * @brief DMA 循环模式软件读指针记账（单调 produced/consumed 计数）
 *
 * 解决模长读写位置在 DMA 恰好接收完整一圈时把"缓冲区已满"误判为"没有数据"
 * 的问题。内部维护单调递增的 32-bit produced/consumed 计数，pending =
 * produced - consumed 永远能区分满与空。
 *
 * 本模块零上层依赖，仅依赖 C 标准库，可直接在 host 单测中验证。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-29
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-29       1.0            zeh            新增 DMA 循环模式环形缓冲记账模块
 *
 */
#ifndef BM_DMA_CIRCULAR_RING_H
#define BM_DMA_CIRCULAR_RING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief DMA 循环模式环形缓冲状态。 */
typedef struct {
    uint8_t *buf;       /**< 缓冲区指针 */
    uint32_t len;       /**< 缓冲区字节数（须 > 0） */
    uint32_t produced;  /**< DMA 累计写入字节数（单调递增） */
    uint32_t consumed;  /**< 软件累计读取字节数（单调递增） */
} bm_dma_circ_ring_t;

/**
 * @brief 复位环形缓冲状态。
 *
 * @param ring 环形缓冲状态
 * @param buf  缓冲区指针
 * @param len  缓冲区字节数
 */
void bm_dma_circ_ring_reset(bm_dma_circ_ring_t *ring,
                            uint8_t *buf, uint32_t len);

/**
 * @brief 根据当前 DMA NDTR 更新 produced 计数。
 *
 * 调用方须保证在 TC 事件时已先调用 bm_dma_circ_ring_mark_full_round()，
 * 否则 NDTR 重载为 len 会导致整圈数据被漏计。
 *
 * @param ring 环形缓冲状态
 * @param ndtr DMA 当前剩余传输字节数
 * @return 本次新增字节数
 */
uint32_t bm_dma_circ_ring_update(bm_dma_circ_ring_t *ring, uint32_t ndtr);

/**
 * @brief 标记 DMA 已完成一整圈（TC 事件时调用）。
 *
 * 将 produced 推进到下一个 len 整数倍边界。
 *
 * @param ring 环形缓冲状态
 */
void bm_dma_circ_ring_mark_full_round(bm_dma_circ_ring_t *ring);

/**
 * @brief 当前未读字节数。
 */
uint32_t bm_dma_circ_ring_pending(const bm_dma_circ_ring_t *ring);

/**
 * @brief 当前空闲字节数。
 */
uint32_t bm_dma_circ_ring_free_space(const bm_dma_circ_ring_t *ring);

/**
 * @brief 丢弃全部已缓冲数据（溢出时调用）。
 */
void bm_dma_circ_ring_drop_all(bm_dma_circ_ring_t *ring);

/**
 * @brief 从环形缓冲读取数据。
 *
 * @param ring    环形缓冲状态
 * @param dst     目标缓冲区
 * @param max_len 最多读取字节数
 * @return 实际读取字节数
 */
uint32_t bm_dma_circ_ring_consume(bm_dma_circ_ring_t *ring,
                                  uint8_t *dst, uint32_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* BM_DMA_CIRCULAR_RING_H */

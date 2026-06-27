/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_dma_stream.h
 * @brief DMA 块流设备驱动 API
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#ifndef BM_DRV_DMA_STREAM_H
#define BM_DRV_DMA_STREAM_H

#include "drv/bm_drv.h"
#include "bm/hybrid/bm_block.h"

struct bm_hal_dma_stream;
#include "bm/common/bm_types.h"

#include <stdint.h>

struct bm_hal_dma_stream;
struct bm_hal_dma_stream_binding;

/** half/full 完成回调 */
typedef void (*bm_hal_dma_stream_block_fn_t)(const struct bm_hal_dma_stream *stream,
                                             bm_block_t *block,
                                             void *context);

/** RX 方向绑定：半满/全满钩子 */
struct bm_hal_dma_stream_binding {
    bm_hal_dma_stream_block_fn_t on_half;
    bm_hal_dma_stream_block_fn_t on_full;
    void                        *context;
};

/** HAL 实例：api + 板级 config */
struct bm_hal_dma_stream {
    const struct bm_dma_stream_driver_api *api;
    const void                          *config;
};

/** DMA 流驱动虚表 */
struct bm_dma_stream_driver_api {
    int (*bind_rx)(const struct bm_hal_dma_stream *dev,
                   const struct bm_hal_dma_stream_binding *binding);
    int (*submit_rx)(const struct bm_hal_dma_stream *dev, bm_block_t *block);
    bm_block_t *(*detach_rx)(const struct bm_hal_dma_stream *dev);
};

#endif /* BM_DRV_DMA_STREAM_H */

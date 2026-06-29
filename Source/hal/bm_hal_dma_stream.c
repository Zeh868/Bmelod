/**
 * @file bm_hal_dma_stream.c
 * @brief DMA 流 HAL 分发层
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            分发至 driver API
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "hal/bm_hal_dma_stream.h"

#include <stddef.h>

static const struct bm_dma_stream_driver_api *dma_api(
    const bm_hal_dma_stream_t *stream) {
    if (stream == NULL || stream->api == NULL) {
        return NULL;
    }
    return stream->api;
}

int bm_hal_dma_stream_bind_rx(const bm_hal_dma_stream_t *stream,
                              const bm_hal_dma_stream_binding_t *binding) {
    const struct bm_dma_stream_driver_api *api = dma_api(stream);

    if (api == NULL || api->bind_rx == NULL) {
        return BM_ERR_NOT_INIT;
    }
    if (binding == NULL) {
        return BM_ERR_INVALID;
    }
    return api->bind_rx(stream, binding);
}

int bm_hal_dma_stream_submit_rx(const bm_hal_dma_stream_t *stream,
                                bm_block_t *block) {
    const struct bm_dma_stream_driver_api *api = dma_api(stream);

    if (api == NULL || api->submit_rx == NULL) {
        return BM_ERR_NOT_INIT;
    }
    if (block == NULL) {
        return BM_ERR_INVALID;
    }
    return api->submit_rx(stream, block);
}

bm_block_t *bm_hal_dma_stream_detach_rx(const bm_hal_dma_stream_t *stream) {
    const struct bm_dma_stream_driver_api *api = dma_api(stream);

    if (api == NULL || api->detach_rx == NULL) {
        return NULL;
    }
    return api->detach_rx(stream);
}

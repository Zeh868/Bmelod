/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_dma_stream_native.c
 * @brief native_sim DMA 块流驱动（模拟 RX 完成回调）
 *
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
#include "bm_hal_dma_stream_sim.h"
#include "bm_log.h"

#include <string.h>

#define TAG "hal_dma"

typedef struct {
    uint32_t id;
} bm_dma_stream_native_config_t;

typedef struct {
    bm_hal_dma_stream_binding_t binding;
    bm_block_t                 *active_block;
    int                         bound;
} dma_stream_sim_state_t;

static dma_stream_sim_state_t g_dma_state[2];

static dma_stream_sim_state_t *dma_state_for(const bm_hal_dma_stream_t *stream) {
    const bm_dma_stream_native_config_t *cfg;

    if (stream == NULL || stream->config == NULL) {
        return NULL;
    }
    cfg = (const bm_dma_stream_native_config_t *)stream->config;
    if (cfg->id >= 2u) {
        return NULL;
    }
    return &g_dma_state[cfg->id];
}

static int native_dma_bind_rx(const struct bm_hal_dma_stream *stream,
                              const struct bm_hal_dma_stream_binding *binding) {
    dma_stream_sim_state_t *state = dma_state_for(stream);

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (binding == NULL) {
        memset(&state->binding, 0, sizeof(state->binding));
        state->bound = 0;
        return BM_OK;
    }
    state->binding = *binding;
    state->bound = 1;
    return BM_OK;
}

static int native_dma_submit_rx(const struct bm_hal_dma_stream *stream,
                                bm_block_t *block) {
    dma_stream_sim_state_t *state = dma_state_for(stream);

    if (state == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }
    if (!state->bound || state->binding.on_full == NULL) {
        return BM_ERR_NOT_INIT;
    }
    if (state->active_block != NULL) {
        BM_LOGW(TAG, "submit_rx: 传输进行中");
        return BM_ERR_BUSY;
    }
    state->active_block = block;
    return BM_OK;
}

static bm_block_t *native_dma_detach_rx(const struct bm_hal_dma_stream *stream) {
    dma_stream_sim_state_t *state = dma_state_for(stream);
    bm_block_t *block;

    if (state == NULL) {
        return NULL;
    }
    block = state->active_block;
    state->active_block = NULL;
    return block;
}

static const struct bm_dma_stream_driver_api bm_dma_stream_native_api = {
    native_dma_bind_rx,
    native_dma_submit_rx,
    native_dma_detach_rx,
};

static const bm_dma_stream_native_config_t bm_dma_stream_cfg0 = { 0u };

const bm_hal_dma_stream_t BM_HAL_DMA_SIM0 = {
    &bm_dma_stream_native_api,
    &bm_dma_stream_cfg0
};

void bm_hal_dma_stream_sim_fire_rx_complete(const bm_hal_dma_stream_t *stream) {
    dma_stream_sim_state_t *state = dma_state_for(stream);
    bm_block_t *block;
    bm_hal_dma_stream_block_fn_t handler;

    if (state == NULL || state->active_block == NULL) {
        return;
    }
    handler = state->binding.on_full;
    if (handler == NULL) {
        return;
    }
    block = state->active_block;
    state->active_block = NULL;
    handler(stream, block, state->binding.context);
}

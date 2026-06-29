/**
 * @file bm_dsp.h
 * @brief 流式 DSP 域聚合头（Block/Frame RT）
 *
 * 在 BM_CONFIG_ENABLE_STREAM 开启时拉入块流、时间戳与 DMA 流 HAL 契约头文件。
 * 应用实现音频、振动或采集前端时可单独包含本头，而无需完整 bm_hybrid.h。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */#ifndef BM_DSP_H
#define BM_DSP_H

#include "bm_config.h"

#if !BM_CONFIG_ENABLE_STREAM
#error "bm_dsp.h requires BM_CONFIG_ENABLE_STREAM"
#endif

#include "bm/hybrid/bm_timestamp.h"
#include "bm/hybrid/bm_block.h"
#include "bm/hybrid/bm_stream.h"
#include "hal/bm_hal_dma_stream.h"

#if BM_CONFIG_ENABLE_PIPELINE
#include "bm/hybrid/bm_pipeline.h"
#endif

#endif /* BM_DSP_H */

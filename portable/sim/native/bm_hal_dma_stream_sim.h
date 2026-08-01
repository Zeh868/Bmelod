/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_dma_stream_sim.h
 * @brief native_sim DMA 流实例与测试辅助
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_DMA_STREAM_SIM_H
#define BM_HAL_DMA_STREAM_SIM_H

#include "hal/bm_hal_dma_stream.h"

extern const bm_hal_dma_stream_t BM_HAL_DMA_SIM0;

/**
 * @brief 在仿真触发路径中执行 bm_hal_dma_stream_sim_fire_rx_complete 回调或状态更新。 模拟一次 RX 传输完成，触发 on_full 回调 */
void bm_hal_dma_stream_sim_fire_rx_complete(const bm_hal_dma_stream_t *stream);

#endif /* BM_HAL_DMA_STREAM_SIM_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_dma_stream.h
 * @brief DMA 块流 HAL 契约（提交缓冲、half/full 回调）
 *
 * 抽象 RX/TX 方向的块缓冲提交与半满/全满事件，供厂商 DMA 驱动或 native_sim
 * 后端实现。与 bm_stream 配合完成 DMA_OWNED ↔ READY 所有权交接。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-12       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 driver API 分发
 * 2026-08-01       1.1            zeh           补全 Doxygen 合规注释
 *
 */
#ifndef BM_HAL_DMA_STREAM_H
#define BM_HAL_DMA_STREAM_H

#include "drv/bm_drv_dma_stream.h"

typedef struct bm_hal_dma_stream bm_hal_dma_stream_t;
typedef struct bm_hal_dma_stream_binding bm_hal_dma_stream_binding_t;

/**
 * @brief 绑定 DMA 接收流
 * @param stream DMA 流设备
 * @param binding 接收绑定配置
 * @return BM_OK 成功；负值表示参数或平台错误
 */
int bm_hal_dma_stream_bind_rx(const bm_hal_dma_stream_t *stream,
                              const bm_hal_dma_stream_binding_t *binding);

/**
 * @brief 提交接收块给 DMA 流
 * @param stream DMA 流设备
 * @param block 待接收的数据块
 * @return BM_OK 成功；负值表示参数或平台错误
 */
int bm_hal_dma_stream_submit_rx(const bm_hal_dma_stream_t *stream,
                                bm_block_t *block);

/**
 * @brief 中止进行中的 RX 并取回尚未交付的数据块
 * @param stream DMA 流设备
 * @return 尚未交付的块；没有可回收块时返回 NULL
 */
bm_block_t *bm_hal_dma_stream_detach_rx(const bm_hal_dma_stream_t *stream);

#endif /* BM_HAL_DMA_STREAM_H */

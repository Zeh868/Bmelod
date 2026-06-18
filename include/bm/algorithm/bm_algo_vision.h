/**
 * @file bm_algo_vision.h
 * @brief 低分辨率视觉扩展：Sobel 梯度、质心与块匹配光流
 *
 * 与 bm_algo_image 互补，面向分拣/安防前端的轻量算子。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_VISION_H
#define BM_ALGO_VISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sobel 梯度（gx/gy 与 src 同尺寸，边界行列为 0） */
void bm_algo_vision_sobel_u8(const uint8_t *src, int16_t *gx, int16_t *gy,
                             uint32_t width, uint32_t height);

/**
 * 二值掩码质心
 * @return 0 成功；-1 无前景像素或参数无效
 */
int bm_algo_vision_centroid_u8(const uint8_t *mask,
                               uint32_t width,
                               uint32_t height,
                               float *cx,
                               float *cy);

/**
 * 块匹配光流：在 search 窗口内最小 SAD
 * @return 0 成功；-1 参数无效
 */
int bm_algo_vision_block_flow_u8(const uint8_t *prev,
                                 const uint8_t *curr,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t bx,
                                 uint32_t by,
                                 uint32_t block_size,
                                 uint32_t search_radius,
                                 int32_t *dx,
                                 int32_t *dy);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_VISION_H */

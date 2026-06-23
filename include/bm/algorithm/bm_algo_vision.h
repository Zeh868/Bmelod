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

/**
 * @brief Sobel 梯度算子（uint8 灰度图）
 *
 * 对输入灰度图计算水平梯度 gx 和垂直梯度 gy，使用 3×3 Sobel 核。
 * 边界行列（首尾各一行/列）保持为 0。
 *
 * @param src    输入灰度图，行主序，尺寸 width × height
 * @param gx     输出水平梯度缓冲区，与 src 同尺寸，调用者分配
 * @param gy     输出垂直梯度缓冲区，与 src 同尺寸，调用者分配
 * @param width  图像宽度（像素），最小值 3
 * @param height 图像高度（像素），最小值 3
 */
void bm_algo_vision_sobel_u8(const uint8_t *src, int16_t *gx, int16_t *gy,
                             uint32_t width, uint32_t height);

/**
 * @brief 二值掩码质心计算
 *
 * 对非零像素求算术平均坐标，得到前景区域的质心。
 *
 * @param mask   输入二值掩码，非零为前景，尺寸 width × height
 * @param width  图像宽度（像素）
 * @param height 图像高度（像素）
 * @param cx     输出质心 x 坐标（浮点）
 * @param cy     输出质心 y 坐标（浮点）
 * @return 0 成功；-1 无前景像素或参数无效
 */
int bm_algo_vision_centroid_u8(const uint8_t *mask,
                               uint32_t width,
                               uint32_t height,
                               float *cx,
                               float *cy);

/**
 * @brief 块匹配光流（SAD 全搜索）
 *
 * 在前帧 prev 中取以 (bx, by) 为左上角、边长 block_size 的参考块，
 * 在当前帧 curr 的 search_radius 范围内穷举搜索最小绝对差之和（SAD），
 * 返回最佳位移向量 (dx, dy)。
 *
 * @param prev          前一帧灰度图，尺寸 width × height
 * @param curr          当前帧灰度图，尺寸 width × height
 * @param width         图像宽度（像素）
 * @param height        图像高度（像素）
 * @param bx            参考块左上角 x 坐标
 * @param by            参考块左上角 y 坐标
 * @param block_size    参考块边长（像素）
 * @param search_radius 搜索半径（像素），在 [-r, +r]×[-r, +r] 内枚举
 * @param dx            输出最佳水平位移
 * @param dy            输出最佳垂直位移
 * @return 0 成功；-1 参数无效或搜索区域完全越界
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

/**
 * @file bm_algo_image.h
 * @brief 低分辨率图像算子：阈值、形态学、连通域与帧差
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            RGB565 转灰度与裁剪
 * 2026-06-17       1.2            zeh            最近邻缩放
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_IMAGE_H
#define BM_ALGO_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void bm_algo_image_threshold_u8(const uint8_t *src, uint8_t *dst,
                              uint32_t width, uint32_t height,
                              uint8_t thresh, uint8_t max_val);

void bm_algo_image_erode_u8(const uint8_t *src, uint8_t *dst,
                            uint32_t width, uint32_t height);
void bm_algo_image_dilate_u8(const uint8_t *src, uint8_t *dst,
                             uint32_t width, uint32_t height);
/* Morphology writes zero borders and requires distinct src/dst buffers. */

typedef struct {
    uint32_t label;
    uint32_t area;
    int32_t  cx;
    int32_t  cy;
} bm_algo_blob_info_t;

#define BM_ALGO_MAX_BLOBS 32u

int bm_algo_image_label_u8(const uint8_t *binary,
                           uint16_t *labels,
                           uint32_t width,
                           uint32_t height,
                           bm_algo_blob_info_t *blobs,
                           uint32_t max_blobs);

void bm_algo_image_frame_diff_u8(const uint8_t *prev,
                                 const uint8_t *curr,
                                 uint8_t *diff,
                                 uint32_t width,
                                 uint32_t height,
                                 uint8_t thresh);

void bm_algo_image_rgb565_to_gray_u8(const uint16_t *rgb565,
                                     uint8_t *gray,
                                     uint32_t width,
                                     uint32_t height);

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} bm_algo_image_crop_rect_t;

/**
 * @brief 从灰度图裁剪子矩形
 *
 * @return 0 成功；-1 参数无效
 */
int bm_algo_image_crop_u8(const uint8_t *src,
                          uint32_t src_width,
                          uint32_t src_height,
                          const bm_algo_image_crop_rect_t *rect,
                          uint8_t *dst);

/**
 * @brief 灰度图最近邻缩放
 *
 * @return 0 成功；-1 参数无效
 */
int bm_algo_image_resize_u8(const uint8_t *src,
                            uint32_t src_width,
                            uint32_t src_height,
                            uint8_t *dst,
                            uint32_t dst_width,
                            uint32_t dst_height);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_IMAGE_H */

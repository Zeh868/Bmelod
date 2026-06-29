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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_IMAGE_H
#define BM_ALGO_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 固定阈值二值化（uint8 灰度图）
 *
 * 逐像素比较：src[i] >= thresh 置为 max_val，否则置 0。
 *
 * @param src      输入灰度图，尺寸 width × height
 * @param dst      输出二值图，与 src 同尺寸，调用者分配（可与 src 相同）
 * @param width    图像宽度（像素）
 * @param height   图像高度（像素）
 * @param thresh   阈值，大于等于此值视为前景
 * @param max_val  前景像素的输出值（通常为 255）
 */
void bm_algo_image_threshold_u8(const uint8_t *src, uint8_t *dst,
                              uint32_t width, uint32_t height,
                              uint8_t thresh, uint8_t max_val);

/**
 * @brief 3×3 矩形结构元腐蚀（uint8 灰度图）
 *
 * 取 3×3 邻域最小值写入输出，边界行列置 0。
 * src 与 dst 不得指向同一缓冲区，图像尺寸最小 3×3。
 *
 * @param src    输入图像，尺寸 width × height
 * @param dst    输出图像，与 src 同尺寸，必须与 src 不同
 * @param width  图像宽度（像素），最小值 3
 * @param height 图像高度（像素），最小值 3
 */
void bm_algo_image_erode_u8(const uint8_t *src, uint8_t *dst,
                            uint32_t width, uint32_t height);

/**
 * @brief 3×3 矩形结构元膨胀（uint8 灰度图）
 *
 * 取 3×3 邻域最大值写入输出，边界行列置 0。
 * src 与 dst 不得指向同一缓冲区，图像尺寸最小 3×3。
 *
 * @param src    输入图像，尺寸 width × height
 * @param dst    输出图像，与 src 同尺寸，必须与 src 不同
 * @param width  图像宽度（像素），最小值 3
 * @param height 图像高度（像素），最小值 3
 */
void bm_algo_image_dilate_u8(const uint8_t *src, uint8_t *dst,
                             uint32_t width, uint32_t height);
/* 形态学算子：边界置零，要求 src 与 dst 为独立缓冲区。 */

/**
 * @brief 连通域标记信息
 */
typedef struct {
    uint32_t label; /**< 连通域标签值（从 1 开始） */
    uint32_t area;  /**< 连通域面积（像素数） */
    int32_t  cx;    /**< 质心 x 坐标（整数近似） */
    int32_t  cy;    /**< 质心 y 坐标（整数近似） */
} bm_algo_blob_info_t;

/** @brief 支持的最大连通域数量 */
#define BM_ALGO_MAX_BLOBS 32u

/**
 * @brief 4 连通域标记（uint8 二值图）
 *
 * 对非零像素进行 4 连通域标记，输出标签图和各连通域统计信息。
 * 标签从 1 开始，0 表示背景。
 * 算法采用重复扫描方式传播标签，无需外部队列，适合嵌入式内存受限场景。
 *
 * @param binary    输入二值图，非零为前景，尺寸 width × height
 * @param labels    输出标签图（uint16），与 binary 同尺寸，调用者分配
 * @param width     图像宽度（像素）
 * @param height    图像高度（像素）
 * @param blobs     输出连通域信息数组，可为 NULL（仅计数不填写）
 * @param max_blobs blobs 数组容量；blobs 为 NULL 时应为 0
 * @return 检测到的连通域总数；-1 表示参数无效或标签值溢出（超过 65535 个域）
 */
int bm_algo_image_label_u8(const uint8_t *binary,
                           uint16_t *labels,
                           uint32_t width,
                           uint32_t height,
                           bm_algo_blob_info_t *blobs,
                           uint32_t max_blobs);

/**
 * @brief 帧差检测（uint8 灰度图）
 *
 * 对相邻两帧逐像素计算绝对差，超过阈值的置 255，否则置 0。
 *
 * @param prev   前一帧灰度图，尺寸 width × height
 * @param curr   当前帧灰度图，尺寸 width × height
 * @param diff   输出差分二值图，尺寸 width × height，调用者分配
 * @param width  图像宽度（像素）
 * @param height 图像高度（像素）
 * @param thresh 差值阈值，绝对差 >= thresh 时判定为运动
 */
void bm_algo_image_frame_diff_u8(const uint8_t *prev,
                                 const uint8_t *curr,
                                 uint8_t *diff,
                                 uint32_t width,
                                 uint32_t height,
                                 uint8_t thresh);

/**
 * @brief RGB565 转灰度（uint8）
 *
 * 使用 ITU-R BT.601 近似系数（Y = 0.299R + 0.587G + 0.114B）将
 * RGB565 像素转换为 8 位灰度值，内部先扩展至 8 位再加权求和。
 *
 * @param rgb565 输入 RGB565 像素数组，尺寸 width × height
 * @param gray   输出灰度图，尺寸 width × height，调用者分配
 * @param width  图像宽度（像素）
 * @param height 图像高度（像素）
 */
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
 * 按 rect 指定的区域从源图中逐行拷贝像素到目标缓冲区。
 * 裁剪区域不得超出源图边界。
 *
 * @param src        输入灰度图，尺寸 src_width × src_height
 * @param src_width  源图宽度（像素）
 * @param src_height 源图高度（像素）
 * @param rect       裁剪矩形，包含左上角坐标及宽高
 * @param dst        输出裁剪结果，尺寸 rect->width × rect->height，调用者分配
 * @return 0 成功；-1 参数无效或裁剪区域越界
 */
int bm_algo_image_crop_u8(const uint8_t *src,
                          uint32_t src_width,
                          uint32_t src_height,
                          const bm_algo_image_crop_rect_t *rect,
                          uint8_t *dst);

/**
 * @brief 灰度图最近邻缩放
 *
 * 将 src_width × src_height 的灰度图缩放到 dst_width × dst_height，
 * 使用最近邻插值，适合嵌入式低功耗场景。
 *
 * @param src        输入灰度图，尺寸 src_width × src_height
 * @param src_width  源图宽度（像素）
 * @param src_height 源图高度（像素）
 * @param dst        输出缩放结果，尺寸 dst_width × dst_height，调用者分配
 * @param dst_width  目标宽度（像素）
 * @param dst_height 目标高度（像素）
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

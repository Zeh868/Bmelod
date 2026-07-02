/**
 * @file bm_algo_image.c
 * @brief 低分辨率图像算子实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            RGB565 转灰度与裁剪
 * 2026-06-17       1.2            zeh            最近邻缩放
 * 2026-06-23       1.2            zeh            补齐 Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_image.h"
#include "bm/algorithm/bm_algo_errors.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

/**
 * @brief 校验图像尺寸并计算像素总数
 *
 * 检查 width 和 height 均不为零、乘积不溢出 uint32_t 且不超过 INT32_MAX，
 * 并将像素总数写入 *count。
 *
 * @param width  图像宽度（像素）
 * @param height 图像高度（像素）
 * @param count  输出像素总数（width × height）
 * @return 0 参数合法；-1 参数无效或溢出
 */
static int image_pixel_count(uint32_t width, uint32_t height, size_t *count) {
    if (count == NULL || width == 0u || height == 0u ||
        width > UINT32_MAX / height ||
        width > INT32_MAX || height > INT32_MAX) {
        return BM_ALGO_ERR_INVALID;
    }
    *count = (size_t)width * (size_t)height;
    return 0;
}

void bm_algo_image_threshold_u8(const uint8_t *src, uint8_t *dst,
                               uint32_t width, uint32_t height,
                               uint8_t thresh, uint8_t max_val) {
    size_t i;
    size_t n;

    if (src == NULL || dst == NULL ||
        image_pixel_count(width, height, &n) != 0) {
        return;
    }

    for (i = 0u; i < n; ++i) {
        dst[i] = (src[i] >= thresh) ? max_val : 0u;
    }
}

void bm_algo_image_erode_u8(const uint8_t *src, uint8_t *dst,
                            uint32_t width, uint32_t height) {
    uint32_t x;
    uint32_t y;
    size_t n;

    if (src == NULL || dst == NULL || src == dst ||
        width < 3u || height < 3u ||
        image_pixel_count(width, height, &n) != 0) {
        return;
    }
    memset(dst, 0, n);

    for (y = 1u; y + 1u < height; ++y) {
        for (x = 1u; x + 1u < width; ++x) {
            uint32_t idx = y * width + x;
            uint8_t min_v = 255u;
            int32_t dy;
            int32_t dx;

            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    uint32_t ny = (uint32_t)((int32_t)y + dy);
                    uint32_t nx = (uint32_t)((int32_t)x + dx);
                    uint8_t v = src[ny * width + nx];
                    if (v < min_v) {
                        min_v = v;
                    }
                }
            }
            dst[idx] = min_v;
        }
    }
}

void bm_algo_image_dilate_u8(const uint8_t *src, uint8_t *dst,
                             uint32_t width, uint32_t height) {
    uint32_t x;
    uint32_t y;
    size_t n;

    if (src == NULL || dst == NULL || src == dst ||
        width < 3u || height < 3u ||
        image_pixel_count(width, height, &n) != 0) {
        return;
    }
    memset(dst, 0, n);

    for (y = 1u; y + 1u < height; ++y) {
        for (x = 1u; x + 1u < width; ++x) {
            uint32_t idx = y * width + x;
            uint8_t max_v = 0u;
            int32_t dy;
            int32_t dx;

            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    uint32_t ny = (uint32_t)((int32_t)y + dy);
                    uint32_t nx = (uint32_t)((int32_t)x + dx);
                    uint8_t v = src[ny * width + nx];
                    if (v > max_v) {
                        max_v = v;
                    }
                }
            }
            dst[idx] = max_v;
        }
    }
}

int bm_algo_image_label_u8(const uint8_t *binary,
                           uint16_t *labels,
                           uint32_t width,
                           uint32_t height,
                           bm_algo_blob_info_t *blobs,
                           uint32_t max_blobs) {
    uint32_t x;
    uint32_t y;
    uint16_t next_label = 1u;
    uint32_t blob_count = 0u;
    size_t pixel_count;

    if (binary == NULL || labels == NULL || width == 0u || height == 0u ||
        (blobs == NULL && max_blobs != 0u) ||
        image_pixel_count(width, height, &pixel_count) != 0 ||
        pixel_count > SIZE_MAX / sizeof(uint16_t)) {
        return BM_ALGO_ERR_INVALID;
    }

    memset(labels, 0, pixel_count * sizeof(uint16_t));

    for (y = 0u; y < height; ++y) {
        for (x = 0u; x < width; ++x) {
            uint32_t idx = y * width + x;

            if (binary[idx] == 0u || labels[idx] != 0u) {
                continue;
            }

            if (next_label == 0u) {
                /* 标签计数器绕回 uint16_t：超过 65535 个连通域，容量溢出。 */
                return BM_ALGO_ERR_OVERFLOW;
            }

            labels[idx] = next_label;
            {
                uint32_t area = 1u;
                uint64_t sum_x = x;
                uint64_t sum_y = y;
                int changed;

                /*
                 * labels[] 同时充当已访问标记。重复扫描策略无需调用者提供队列，
                 * 仍可正确标记真正的 4 连通域；代价是对大图复杂度偏高，
                 * 适合嵌入式小分辨率场景（详见 bm_algo_image_label_u8 说明）。
                 */
                do {
                    uint32_t scan_x;
                    uint32_t scan_y;

                    changed = 0;
                    for (scan_y = 0u; scan_y < height; ++scan_y) {
                        for (scan_x = 0u; scan_x < width; ++scan_x) {
                            uint32_t current = scan_y * width + scan_x;
                            int connected = 0;

                            if (binary[current] == 0u || labels[current] != 0u) {
                                continue;
                            }
                            if (scan_x > 0u &&
                                labels[current - 1u] == next_label) {
                                connected = 1;
                            } else if (scan_x + 1u < width &&
                                       labels[current + 1u] == next_label) {
                                connected = 1;
                            } else if (scan_y > 0u &&
                                       labels[current - width] == next_label) {
                                connected = 1;
                            } else if (scan_y + 1u < height &&
                                       labels[current + width] == next_label) {
                                connected = 1;
                            }

                            if (connected) {
                                labels[current] = next_label;
                                area++;
                                sum_x += scan_x;
                                sum_y += scan_y;
                                changed = 1;
                            }
                        }
                    }
                } while (changed);

                if (blobs != NULL && blob_count < max_blobs) {
                    blobs[blob_count].label = next_label;
                    blobs[blob_count].area = area;
                    blobs[blob_count].cx = (int32_t)(sum_x / area);
                    blobs[blob_count].cy = (int32_t)(sum_y / area);
                }
            }

            if (blobs == NULL || blob_count < max_blobs) {
                blob_count++;
            }
            next_label++;
        }
    }

    return (int)blob_count;
}

void bm_algo_image_frame_diff_u8(const uint8_t *prev,
                                 const uint8_t *curr,
                                 uint8_t *diff,
                                 uint32_t width,
                                 uint32_t height,
                                 uint8_t thresh) {
    size_t i;
    size_t n;

    if (prev == NULL || curr == NULL || diff == NULL ||
        image_pixel_count(width, height, &n) != 0) {
        return;
    }

    for (i = 0u; i < n; ++i) {
        int d = (int)curr[i] - (int)prev[i];
        if (d < 0) {
            d = -d;
        }
        diff[i] = ((uint8_t)d >= thresh) ? 255u : 0u;
    }
}

void bm_algo_image_rgb565_to_gray_u8(const uint16_t *rgb565,
                                     uint8_t *gray,
                                     uint32_t width,
                                     uint32_t height) {
    size_t i;
    size_t n;

    if (rgb565 == NULL || gray == NULL ||
        image_pixel_count(width, height, &n) != 0) {
        return;
    }

    for (i = 0u; i < n; ++i) {
        uint16_t px = rgb565[i];
        uint32_t r5 = (uint32_t)(px >> 11) & 0x1Fu;
        uint32_t g6 = (uint32_t)(px >> 5) & 0x3Fu;
        uint32_t b5 = (uint32_t)px & 0x1Fu;
        uint32_t r8 = (r5 << 3) | (r5 >> 2);
        uint32_t g8 = (g6 << 2) | (g6 >> 4);
        uint32_t b8 = (b5 << 3) | (b5 >> 2);
        uint32_t y = (77u * r8 + 150u * g8 + 29u * b8) >> 8;

        gray[i] = (uint8_t)(y > 255u ? 255u : y);
    }
}

int bm_algo_image_crop_u8(const uint8_t *src,
                          uint32_t src_width,
                          uint32_t src_height,
                          const bm_algo_image_crop_rect_t *rect,
                          uint8_t *dst) {
    uint32_t row;
    size_t src_n;

    if (src == NULL || rect == NULL || dst == NULL ||
        src_width == 0u || src_height == 0u ||
        rect->width == 0u || rect->height == 0u ||
        image_pixel_count(src_width, src_height, &src_n) != 0) {
        return BM_ALGO_ERR_INVALID;
    }

    /* 越界检查用减法避免 u32 加法回绕（x 接近 UINT32_MAX 时 x+width 溢出）。 */
    if (rect->x >= src_width || rect->width > src_width - rect->x ||
        rect->y >= src_height || rect->height > src_height - rect->y) {
        return BM_ALGO_ERR_INVALID;
    }

    for (row = 0u; row < rect->height; ++row) {
        const uint8_t *src_row =
            src + (rect->y + row) * src_width + rect->x;
        uint8_t *dst_row = dst + row * rect->width;

        memcpy(dst_row, src_row, (size_t)rect->width);
    }
    return 0;
}

int bm_algo_image_resize_u8(const uint8_t *src,
                            uint32_t src_width,
                            uint32_t src_height,
                            uint8_t *dst,
                            uint32_t dst_width,
                            uint32_t dst_height) {
    uint32_t y;
    uint32_t x;
    size_t src_n;

    if (src == NULL || dst == NULL ||
        src_width == 0u || src_height == 0u ||
        dst_width == 0u || dst_height == 0u ||
        image_pixel_count(src_width, src_height, &src_n) != 0) {
        return BM_ALGO_ERR_INVALID;
    }

    for (y = 0u; y < dst_height; ++y) {
        uint32_t src_y = y * src_height / dst_height;
        for (x = 0u; x < dst_width; ++x) {
            uint32_t src_x = x * src_width / dst_width;
            dst[y * dst_width + x] = src[src_y * src_width + src_x];
        }
    }
    return 0;
}

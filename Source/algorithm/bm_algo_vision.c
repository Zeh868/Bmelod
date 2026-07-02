/**
 * @file bm_algo_vision.c
 * @brief 低分辨率视觉扩展实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       1.0            zeh            补齐 Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_vision.h"
#include "bm/algorithm/bm_algo_errors.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

void bm_algo_vision_sobel_u8(const uint8_t *src, int16_t *gx, int16_t *gy,
                             uint32_t width, uint32_t height) {
    uint32_t x;
    uint32_t y;
    size_t pixel_count;

    if (src == NULL || gx == NULL || gy == NULL ||
        width < 3u || height < 3u ||
        width > UINT32_MAX / height ||
        (size_t)width * (size_t)height >
            SIZE_MAX / sizeof(int16_t)) {
        return;
    }

    pixel_count = (size_t)width * (size_t)height;
    memset(gx, 0, pixel_count * sizeof(int16_t));
    memset(gy, 0, pixel_count * sizeof(int16_t));

    for (y = 1u; y + 1u < height; ++y) {
        for (x = 1u; x + 1u < width; ++x) {
            uint32_t idx = y * width + x;
            const uint8_t *r0 = src + (y - 1u) * width;
            const uint8_t *r1 = src + y * width;
            const uint8_t *r2 = src + (y + 1u) * width;
            int32_t gx_v =
                -(int32_t)r0[x - 1u] + (int32_t)r0[x + 1u] +
                -2 * (int32_t)r1[x - 1u] + 2 * (int32_t)r1[x + 1u] +
                -(int32_t)r2[x - 1u] + (int32_t)r2[x + 1u];
            int32_t gy_v =
                -(int32_t)r0[x - 1u] - 2 * (int32_t)r0[x] - (int32_t)r0[x + 1u] +
                (int32_t)r2[x - 1u] + 2 * (int32_t)r2[x] + (int32_t)r2[x + 1u];
            gx[idx] = (int16_t)gx_v;
            gy[idx] = (int16_t)gy_v;
        }
    }
}

int bm_algo_vision_centroid_u8(const uint8_t *mask,
                               uint32_t width,
                               uint32_t height,
                               float *cx,
                               float *cy) {
    uint32_t x;
    uint32_t y;
    uint32_t count = 0u;
    float sum_x = 0.0f;
    float sum_y = 0.0f;

    if (mask == NULL || cx == NULL || cy == NULL || width == 0u || height == 0u) {
        return BM_ALGO_ERR_INVALID;
    }
    if (width > UINT32_MAX / height) {
        /* width * height 相乘会溢出 uint32_t。 */
        return BM_ALGO_ERR_OVERFLOW;
    }

    for (y = 0u; y < height; ++y) {
        for (x = 0u; x < width; ++x) {
            if (mask[y * width + x] != 0u) {
                sum_x += (float)x;
                sum_y += (float)y;
                count++;
            }
        }
    }

    if (count == 0u) {
        /* mask 内无前景像素，无质心可计算。 */
        return BM_ALGO_ERR_NOT_FOUND;
    }

    *cx = sum_x / (float)count;
    *cy = sum_y / (float)count;
    return 0;
}

int bm_algo_vision_block_flow_u8(const uint8_t *prev,
                                 const uint8_t *curr,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t bx,
                                 uint32_t by,
                                 uint32_t block_size,
                                 uint32_t search_radius,
                                 int32_t *dx,
                                 int32_t *dy) {
    uint64_t best_sad = UINT64_MAX;
    int32_t best_dx = 0;
    int32_t best_dy = 0;
    int32_t sdy;
    int32_t sdx;

    if (prev == NULL || curr == NULL || dx == NULL || dy == NULL ||
        block_size == 0u || bx > width || by > height ||
        block_size > width - bx || block_size > height - by ||
        bx > INT32_MAX || by > INT32_MAX ||
        search_radius >= INT32_MAX) {
        return BM_ALGO_ERR_INVALID;
    }

    for (sdy = -(int32_t)search_radius; sdy <= (int32_t)search_radius; ++sdy) {
        for (sdx = -(int32_t)search_radius; sdx <= (int32_t)search_radius; ++sdx) {
            uint32_t x;
            uint32_t y;
            uint64_t sad = 0u;
            int64_t cx0 = (int64_t)bx + (int64_t)sdx;
            int64_t cy0 = (int64_t)by + (int64_t)sdy;

            if (cx0 < 0 || cy0 < 0 ||
                (uint64_t)cx0 > width ||
                (uint64_t)cy0 > height ||
                block_size > width - (uint32_t)cx0 ||
                block_size > height - (uint32_t)cy0) {
                continue;
            }

            for (y = 0u; y < block_size; ++y) {
                for (x = 0u; x < block_size; ++x) {
                    int32_t a = (int32_t)prev[(by + y) * width + (bx + x)];
                    int32_t b = (int32_t)curr[
                        ((uint32_t)cy0 + y) * width + ((uint32_t)cx0 + x)];
                    int32_t d = a - b;
                    if (d < 0) {
                        d = -d;
                    }
                    sad += (uint32_t)d;
                }
            }

            if (sad < best_sad) {
                best_sad = sad;
                best_dx = sdx;
                best_dy = sdy;
            }
        }
    }

    *dx = best_dx;
    *dy = best_dy;
    if (best_sad == UINT64_MAX) {
        /* 搜索窗口内所有候选位置均越界，未产生任何有效匹配。 */
        return BM_ALGO_ERR_NOT_FOUND;
    }
    return 0;
}

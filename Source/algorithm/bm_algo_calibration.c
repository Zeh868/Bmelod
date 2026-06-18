/**
 * @file bm_algo_calibration.c
 * @brief 标定与插值实现
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
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_calibration.h"
#include <stddef.h>

float bm_algo_lut1d_interp(const bm_algo_lut1d_t *lut, float x) {
    uint32_t i;

    if (lut == NULL || lut->x == NULL || lut->y == NULL ||
        lut->point_count == 0u) {
        return 0.0f;
    }

    if (x <= lut->x[0]) {
        return lut->y[0];
    }
    if (x >= lut->x[lut->point_count - 1u]) {
        return lut->y[lut->point_count - 1u];
    }

    for (i = 0u; i + 1u < lut->point_count; ++i) {
        if (x >= lut->x[i] && x <= lut->x[i + 1u]) {
            float span = lut->x[i + 1u] - lut->x[i];
            float t;

            if (span <= 0.0f) {
                return lut->y[i];
            }
            t = (x - lut->x[i]) / span;
            return lut->y[i] + t * (lut->y[i + 1u] - lut->y[i]);
        }
    }

    return lut->y[lut->point_count - 1u];
}

float bm_algo_linear_cal_apply(const bm_algo_linear_cal_t *cal, float raw) {
    if (cal == NULL) {
        return raw;
    }
    return cal->gain * raw + cal->offset;
}

/**
 * @file bm_algo_calibration.h
 * @brief 标定与插值：分段线性、多项式最小二乘（2 阶）
 *
 * @maturity E1
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
#ifndef BM_ALGO_CALIBRATION_H
#define BM_ALGO_CALIBRATION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const float *x;
    const float *y;
    uint32_t point_count;
} bm_algo_lut1d_t;

float bm_algo_lut1d_interp(const bm_algo_lut1d_t *lut, float x);

/** 线性标定：y = gain * raw + offset */
typedef struct {
    float gain;
    float offset;
} bm_algo_linear_cal_t;

float bm_algo_linear_cal_apply(const bm_algo_linear_cal_t *cal, float raw);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_CALIBRATION_H */

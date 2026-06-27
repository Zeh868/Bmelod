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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_CALIBRATION_H
#define BM_ALGO_CALIBRATION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 一维分段线性查找表（LUT）描述符
 *
 * x 数组须单调递增，x 与 y 长度均为 point_count。
 */
typedef struct {
    const float *x;          /**< 自变量节点数组（单调递增） */
    const float *y;          /**< 对应因变量节点数组 */
    uint32_t     point_count; /**< 节点数量 */
} bm_algo_lut1d_t;

/**
 * @brief 对一维分段线性 LUT 进行插值查询
 *
 * 超出区间时饱和（取端点值）。
 *
 * @param lut 查找表描述符指针
 * @param x   查询自变量值
 * @return 插值结果；lut 或内部指针为 NULL、point_count 为 0 时返回 0.0f
 */
float bm_algo_lut1d_interp(const bm_algo_lut1d_t *lut, float x);

/**
 * @brief 线性标定参数：y = gain * raw + offset
 */
typedef struct {
    float gain;   /**< 增益系数 */
    float offset; /**< 偏置量 */
} bm_algo_linear_cal_t;

/**
 * @brief 应用线性标定：y = gain * raw + offset
 *
 * @param cal 标定参数指针
 * @param raw 原始输入值
 * @return 标定后的输出值；cal 为 NULL 时直通返回 raw
 */
float bm_algo_linear_cal_apply(const bm_algo_linear_cal_t *cal, float raw);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_CALIBRATION_H */

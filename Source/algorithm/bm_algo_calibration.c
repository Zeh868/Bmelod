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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_calibration.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

/**
 * @brief 对一维分段线性 LUT 进行插值查询
 *
 * 算法：遍历节点找到包含 x 的区间 [x[i], x[i+1]]，
 * 计算归一化参数 t = (x - x[i]) / (x[i+1] - x[i])，
 * 返回 y[i] + t * (y[i+1] - y[i])。
 * 超出区间时饱和（取端点值）；非单调（跨度≤0）区间跳过并继续向后查找，
 * 无合法区间时返回末端 y 值。
 *
 * @param lut 查找表描述符指针
 * @param x   查询自变量值
 * @return 插值结果；lut 或内部指针为 NULL、point_count 为 0 时返回 0.0f
 */
float bm_algo_lut1d_interp(const bm_algo_lut1d_t *lut, float x) {
    uint32_t i;

    if (lut == NULL || lut->x == NULL || lut->y == NULL ||
        lut->point_count == 0u) {
        return 0.0f;
    }

    /* Batch-3：NaN 输入若饱和到末端值，会比传播 NaN 更危险（静默错误满量程输出）；
     * 非有限输入直接透传，让下游可见异常。 */
    if (!bm_algo_is_finite_f(x)) {
        return x;
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

            /* Batch-3：非单调区间跳过而非立即返回左端点，继续向后查找合法区间 */
            if (span <= 0.0f) {
                continue;
            }
            t = (x - lut->x[i]) / span;
            return lut->y[i] + t * (lut->y[i + 1u] - lut->y[i]);
        }
    }

    return lut->y[lut->point_count - 1u];
}

/**
 * @brief 应用线性标定：y = gain * raw + offset
 *
 * @param cal 标定参数指针（gain/offset）
 * @param raw 原始输入值
 * @return 标定后的输出值；cal 为 NULL 时直通返回 raw
 */
float bm_algo_linear_cal_apply(const bm_algo_linear_cal_t *cal, float raw) {
    if (cal == NULL) {
        return raw;
    }
    return cal->gain * raw + cal->offset;
}

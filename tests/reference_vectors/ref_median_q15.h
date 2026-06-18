/**
 * @file ref_median_q15.h
 * @brief Q15 中值滤波窗口 3 黄金参考
 *
 * 输入序列 100, 5000, 200 → 中值 200（Q15）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始黄金向量
 */
#ifndef REF_MEDIAN_Q15_H
#define REF_MEDIAN_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_MEDIAN_Q15_WINDOW         3u
#define REF_MEDIAN_Q15_GOLDEN_COUNT   3u
#define REF_MEDIAN_Q15_TOLERANCE      ((bm_algo_q15_t)2)

static const bm_algo_q15_t ref_median_q15_inputs[REF_MEDIAN_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)327,
    (bm_algo_q15_t)16384,
    (bm_algo_q15_t)655
};

static const bm_algo_q15_t ref_median_q15_golden[REF_MEDIAN_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)327,
    (bm_algo_q15_t)16384,
    (bm_algo_q15_t)655
};

#endif /* REF_MEDIAN_Q15_H */

/**
 * @file ref_differentiator_q15.h
 * @brief Q15 微分器黄金参考向量
 *
 * coeff=0.5、dt=0.1 s；四步输入 0→0.05→0.05→0.15（Q15 归一化）。
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
#ifndef REF_DIFFERENTIATOR_Q15_H
#define REF_DIFFERENTIATOR_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_DIFFERENTIATOR_Q15_COEFF      ((bm_algo_q15_t)16383)
#define REF_DIFFERENTIATOR_Q15_DT         ((bm_algo_q15_t)3276)
#define REF_DIFFERENTIATOR_Q15_STEP_COUNT 4u

static const bm_algo_q15_t ref_differentiator_q15_inputs[REF_DIFFERENTIATOR_Q15_STEP_COUNT] = {
    (bm_algo_q15_t)0,
    (bm_algo_q15_t)1638,
    (bm_algo_q15_t)1638,
    (bm_algo_q15_t)4915
};

/** 四步微分输出期望（Q15） */
static const bm_algo_q15_t ref_differentiator_q15_golden[REF_DIFFERENTIATOR_Q15_STEP_COUNT] = {
    (bm_algo_q15_t)0,
    (bm_algo_q15_t)8191,
    (bm_algo_q15_t)4095,
    (bm_algo_q15_t)18433
};

#define REF_DIFFERENTIATOR_Q15_TOLERANCE  ((bm_algo_q15_t)50)

#endif /* REF_DIFFERENTIATOR_Q15_H */

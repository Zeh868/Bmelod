/**
 * @file ref_integrator_q15.h
 * @brief Q15 积分器黄金参考向量
 *
 * 输入 0.5、dt=0.01s、限幅 [0,1]，五步积分期望值。
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
#ifndef REF_INTEGRATOR_Q15_H
#define REF_INTEGRATOR_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_INTEGRATOR_Q15_INPUT        ((bm_algo_q15_t)16383)
#define REF_INTEGRATOR_Q15_DT           ((bm_algo_q15_t)328)
#define REF_INTEGRATOR_Q15_MIN          ((bm_algo_q15_t)0)
#define REF_INTEGRATOR_Q15_MAX          BM_ALGO_Q15_ONE
#define REF_INTEGRATOR_Q15_GOLDEN_COUNT 5u
#define REF_INTEGRATOR_Q15_WARMUP       100u
#define REF_INTEGRATOR_Q15_FINAL        ((bm_algo_q15_t)16300)

/** 五步积分器输出期望（Q15） */
static const bm_algo_q15_t ref_integrator_q15_golden[REF_INTEGRATOR_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)163,
    (bm_algo_q15_t)326,
    (bm_algo_q15_t)489,
    (bm_algo_q15_t)652,
    (bm_algo_q15_t)815
};

#define REF_INTEGRATOR_Q15_TOLERANCE    ((bm_algo_q15_t)5)

#endif /* REF_INTEGRATOR_Q15_H */

/**
 * @file ref_lead_lag_q15.h
 * @brief Q15 超前滞后黄金参考向量
 *
 * 直通系数 b0=1、b1=0、a1=0，四步阶跃输入 0→0.5→0.5→1.0。
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
#ifndef REF_LEAD_LAG_Q15_H
#define REF_LEAD_LAG_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_LEAD_LAG_Q15_B0             BM_ALGO_Q15_ONE
#define REF_LEAD_LAG_Q15_B1             ((bm_algo_q15_t)0)
#define REF_LEAD_LAG_Q15_A1             ((bm_algo_q15_t)0)
#define REF_LEAD_LAG_Q15_GOLDEN_COUNT   4u

static const bm_algo_q15_t ref_lead_lag_q15_inputs[REF_LEAD_LAG_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)0,
    (bm_algo_q15_t)16383,
    (bm_algo_q15_t)16383,
    BM_ALGO_Q15_ONE
};

/** 四步输出期望（Q15） */
static const bm_algo_q15_t ref_lead_lag_q15_golden[REF_LEAD_LAG_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)0,
    (bm_algo_q15_t)16383,
    (bm_algo_q15_t)16383,
    (bm_algo_q15_t)32766
};

#define REF_LEAD_LAG_Q15_TOLERANCE      ((bm_algo_q15_t)5)

#endif /* REF_LEAD_LAG_Q15_H */

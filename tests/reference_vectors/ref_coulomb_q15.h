/**
 * @file ref_coulomb_q15.h
 * @brief Q15 库仑计量恒流放电 SOC 黄金参考向量
 *
 * 初始 SOC=0.5，C-rate=-0.1，dt=1.0 小时比例，容量/效率满幅。
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
#ifndef REF_COULOMB_Q15_H
#define REF_COULOMB_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_COULOMB_Q15_SOC_INIT        ((bm_algo_q15_t)16383)
#define REF_COULOMB_Q15_CAPACITY        BM_ALGO_Q15_ONE
#define REF_COULOMB_Q15_EFFICIENCY      BM_ALGO_Q15_ONE
#define REF_COULOMB_Q15_CURRENT         ((bm_algo_q15_t)-3276)
#define REF_COULOMB_Q15_DT              BM_ALGO_Q15_ONE
#define REF_COULOMB_Q15_STEP_COUNT      5u

/** 每步 SOC 期望（Q15） */
static const bm_algo_q15_t ref_coulomb_q15_golden[REF_COULOMB_Q15_STEP_COUNT] = {
    (bm_algo_q15_t)13107,
    (bm_algo_q15_t)9831,
    (bm_algo_q15_t)6555,
    (bm_algo_q15_t)3279,
    (bm_algo_q15_t)3
};

#define REF_COULOMB_Q15_TOLERANCE       ((bm_algo_q15_t)10)

#endif /* REF_COULOMB_Q15_H */

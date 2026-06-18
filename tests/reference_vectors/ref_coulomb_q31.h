/**
 * @file ref_coulomb_q31.h
 * @brief Q31 库仑计量恒流放电 SOC 黄金参考向量
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
#ifndef REF_COULOMB_Q31_H
#define REF_COULOMB_Q31_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_COULOMB_Q31_SOC_INIT        ((bm_algo_q31_t)1073741824)
#define REF_COULOMB_Q31_CAPACITY        BM_ALGO_Q31_ONE
#define REF_COULOMB_Q31_EFFICIENCY      BM_ALGO_Q31_ONE
#define REF_COULOMB_Q31_CURRENT         ((bm_algo_q31_t)-214748365)
#define REF_COULOMB_Q31_DT              BM_ALGO_Q31_ONE
#define REF_COULOMB_Q31_STEP_COUNT      5u

/** 每步 SOC 期望（Q31） */
static const bm_algo_q31_t ref_coulomb_q31_golden[REF_COULOMB_Q31_STEP_COUNT] = {
    (bm_algo_q31_t)858993456,
    (bm_algo_q31_t)644245088,
    (bm_algo_q31_t)429496720,
    (bm_algo_q31_t)214748352,
    (bm_algo_q31_t)0
};

#define REF_COULOMB_Q31_TOLERANCE       ((bm_algo_q31_t)500000)

#endif /* REF_COULOMB_Q31_H */

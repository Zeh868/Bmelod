/**
 * @file ref_mppt_po_q31.h
 * @brief Q31 P&O MPPT 功率下降翻转方向黄金参考向量
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
#ifndef REF_MPPT_PO_Q31_H
#define REF_MPPT_PO_Q31_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_MPPT_PO_Q31_V_INIT        ((bm_algo_q31_t)858993459)
#define REF_MPPT_PO_Q31_STEP_V        ((bm_algo_q31_t)21474836)
#define REF_MPPT_PO_Q31_V_MIN         ((bm_algo_q31_t)0)
#define REF_MPPT_PO_Q31_V_MAX         BM_ALGO_Q31_ONE
#define REF_MPPT_PO_Q31_GOLDEN_COUNT  3u
#define REF_MPPT_PO_Q31_TOLERANCE     ((bm_algo_q31_t)4)

static const bm_algo_q31_t ref_mppt_po_q31_golden[REF_MPPT_PO_Q31_GOLDEN_COUNT] = {
    (bm_algo_q31_t)880468295,
    (bm_algo_q31_t)858993459,
    (bm_algo_q31_t)880468295
};

static const bm_algo_q31_t ref_mppt_po_q31_voltage[REF_MPPT_PO_Q31_GOLDEN_COUNT] = {
    REF_MPPT_PO_Q31_V_INIT,
    (bm_algo_q31_t)880468296,
    (bm_algo_q31_t)858993459
};

static const bm_algo_q31_t ref_mppt_po_q31_current[REF_MPPT_PO_Q31_GOLDEN_COUNT] = {
    (bm_algo_q31_t)1308622848,
    (bm_algo_q31_t)1242321920,
    (bm_algo_q31_t)1176020992
};

#endif /* REF_MPPT_PO_Q31_H */

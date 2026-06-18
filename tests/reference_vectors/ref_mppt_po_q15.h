/**
 * @file ref_mppt_po_q15.h
 * @brief Q15 P&O MPPT 功率下降翻转方向黄金参考向量
 *
 * v_init=0.4、step=0.01；三步电压/电流使功率递减并翻转扰动方向。
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
#ifndef REF_MPPT_PO_Q15_H
#define REF_MPPT_PO_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_MPPT_PO_Q15_V_INIT        ((bm_algo_q15_t)13106)
#define REF_MPPT_PO_Q15_STEP_V        ((bm_algo_q15_t)327)
#define REF_MPPT_PO_Q15_V_MIN         ((bm_algo_q15_t)0)
#define REF_MPPT_PO_Q15_V_MAX         BM_ALGO_Q15_ONE
#define REF_MPPT_PO_Q15_GOLDEN_COUNT  3u

/** 三步 v_ref 期望（Q15，功率下降后方向翻转） */
static const bm_algo_q15_t ref_mppt_po_q15_golden[REF_MPPT_PO_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)13433,
    (bm_algo_q15_t)13106,
    (bm_algo_q15_t)13433
};

/** 三步输入电压（Q15） */
static const bm_algo_q15_t ref_mppt_po_q15_voltage[REF_MPPT_PO_Q15_GOLDEN_COUNT] = {
    REF_MPPT_PO_Q15_V_INIT,
    (bm_algo_q15_t)13434,
    (bm_algo_q15_t)13106
};

/** 三步输入电流（Q15） */
static const bm_algo_q15_t ref_mppt_po_q15_current[REF_MPPT_PO_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)19988,
    (bm_algo_q15_t)19005,
    (bm_algo_q15_t)18022
};

#define REF_MPPT_PO_Q15_TOLERANCE     ((bm_algo_q15_t)2)

#endif /* REF_MPPT_PO_Q15_H */

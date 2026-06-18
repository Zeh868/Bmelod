/**
 * @file ref_lpf1_q31.h
 * @brief Q31 一阶低通滤波器黄金参考向量
 *
 * alpha=0.1，输入在 step=0 起阶跃至 1.0（Q31 满幅），期望输出为定点递推结果。
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
#ifndef REF_LPF1_Q31_H
#define REF_LPF1_Q31_H

#include "bm/algorithm/bm_algo_fixed.h"

/** 低通系数 alpha=0.1（Q31） */
#define REF_LPF1_Q31_ALPHA          ((bm_algo_q31_t)214748365)

/** 阶跃前保持 0 的采样数 */
#define REF_LPF1_Q31_WARMUP_STEPS   2u

/** 阶跃后用于比对的输出点数 */
#define REF_LPF1_Q31_GOLDEN_COUNT   5u

/** 阶跃输入（Q31 满幅） */
#define REF_LPF1_Q31_STEP_INPUT     BM_ALGO_Q31_ONE

/** 阶跃后期望输出（Q31，手算与 float 0.1 一阶低通对齐） */
static const bm_algo_q31_t ref_lpf1_q31_golden[REF_LPF1_Q31_GOLDEN_COUNT] = {
    (bm_algo_q31_t)214748365,
    (bm_algo_q31_t)408021893,
    (bm_algo_q31_t)581819704,
    (bm_algo_q31_t)738637534,
    (bm_algo_q31_t)880773681
};

/** 比对容差（Q31 绝对值） */
#define REF_LPF1_Q31_TOLERANCE      ((bm_algo_q31_t)5000000)

#endif /* REF_LPF1_Q31_H */

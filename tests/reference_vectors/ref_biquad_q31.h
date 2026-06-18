/**
 * @file ref_biquad_q31.h
 * @brief Q31 二阶 IIR 黄金参考向量
 *
 * 直通系数 b0=1.0，其余为 0；阶跃输入 Q31 满幅，输出应跟踪输入。
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
#ifndef REF_BIQUAD_Q31_H
#define REF_BIQUAD_Q31_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_BIQUAD_Q31_STEP_INPUT   BM_ALGO_Q31_ONE
#define REF_BIQUAD_Q31_GOLDEN_COUNT 3u

/** 直通滤波器三步输出期望（Q31） */
static const bm_algo_q31_t ref_biquad_q31_golden[REF_BIQUAD_Q31_GOLDEN_COUNT] = {
    BM_ALGO_Q31_ONE,
    BM_ALGO_Q31_ONE,
    BM_ALGO_Q31_ONE
};

#define REF_BIQUAD_Q31_TOLERANCE    ((bm_algo_q31_t)2)

#endif /* REF_BIQUAD_Q31_H */

/**
 * @file ref_trapezoid_q31.h
 * @brief Q31 梯形速度曲线黄金参考向量
 *
 * max_vel/accel/decel=0.5，dt=0.1s，目标 1.0，起点 0/0。
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
#ifndef REF_TRAPEZOID_Q31_H
#define REF_TRAPEZOID_Q31_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_TRAPEZOID_Q31_MAX_VEL       ((bm_algo_q31_t)1073741824)
#define REF_TRAPEZOID_Q31_MAX_ACCEL     ((bm_algo_q31_t)1073741824)
#define REF_TRAPEZOID_Q31_MAX_DECEL     ((bm_algo_q31_t)1073741824)
#define REF_TRAPEZOID_Q31_DT            ((bm_algo_q31_t)214748365)
#define REF_TRAPEZOID_Q31_TARGET        BM_ALGO_Q31_ONE
#define REF_TRAPEZOID_Q31_GOLDEN_COUNT  5u

/** 五步位置期望（Q31） */
static const bm_algo_q31_t ref_trapezoid_q31_golden[REF_TRAPEZOID_Q31_GOLDEN_COUNT] = {
    (bm_algo_q31_t)10737418,
    (bm_algo_q31_t)32212255,
    (bm_algo_q31_t)64424510,
    (bm_algo_q31_t)107374184,
    (bm_algo_q31_t)161061276
};

#define REF_TRAPEZOID_Q31_TOLERANCE     ((bm_algo_q31_t)500000)

#endif /* REF_TRAPEZOID_Q31_H */

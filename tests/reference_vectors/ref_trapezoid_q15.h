/**
 * @file ref_trapezoid_q15.h
 * @brief Q15 梯形速度曲线黄金参考向量
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
#ifndef REF_TRAPEZOID_Q15_H
#define REF_TRAPEZOID_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_TRAPEZOID_Q15_MAX_VEL     ((bm_algo_q15_t)16383)
#define REF_TRAPEZOID_Q15_MAX_ACCEL   ((bm_algo_q15_t)16383)
#define REF_TRAPEZOID_Q15_MAX_DECEL   ((bm_algo_q15_t)16383)
#define REF_TRAPEZOID_Q15_DT          ((bm_algo_q15_t)3277)
#define REF_TRAPEZOID_Q15_TARGET      BM_ALGO_Q15_ONE
#define REF_TRAPEZOID_Q15_GOLDEN_COUNT  5u

/** 五步位置期望（Q15） */
static const bm_algo_q15_t ref_trapezoid_q15_golden[REF_TRAPEZOID_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)163,
    (bm_algo_q15_t)490,
    (bm_algo_q15_t)981,
    (bm_algo_q15_t)1636,
    (bm_algo_q15_t)2454
};

#define REF_TRAPEZOID_Q15_TOLERANCE   ((bm_algo_q15_t)5)

#endif /* REF_TRAPEZOID_Q15_H */

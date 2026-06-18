/**
 * @file ref_scurve_q15.h
 * @brief Q15 S 曲线位置黄金参考向量（float 桥接对照）
 *
 * max_vel/accel=0.5、max_jerk=1.0、dt=0.1s，目标 1.0，起点 0/0/0。
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
#ifndef REF_SCURVE_Q15_H
#define REF_SCURVE_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_SCURVE_Q15_MAX_VEL        ((bm_algo_q15_t)16383)
#define REF_SCURVE_Q15_MAX_ACCEL      ((bm_algo_q15_t)16383)
#define REF_SCURVE_Q15_MAX_JERK       BM_ALGO_Q15_ONE
#define REF_SCURVE_Q15_DT             ((bm_algo_q15_t)3277)
#define REF_SCURVE_Q15_TARGET         BM_ALGO_Q15_ONE
#define REF_SCURVE_Q15_GOLDEN_COUNT   5u

/** 五步位置期望（Q15，经 bm_algo_scurve_step 桥接生成） */
static const bm_algo_q15_t ref_scurve_q15_golden[REF_SCURVE_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)32,
    (bm_algo_q15_t)131,
    (bm_algo_q15_t)327,
    (bm_algo_q15_t)655,
    (bm_algo_q15_t)1146
};

#define REF_SCURVE_Q15_TOLERANCE      ((bm_algo_q15_t)8)

#endif /* REF_SCURVE_Q15_H */

/**
 * @file ref_pi_q15.h
 * @brief Q15 PI 控制器阶跃黄金参考向量
 *
 * kp=0.8、ki=0.5、error=1.0、dt=0.01s，五步输出期望。
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
#ifndef REF_PI_Q15_H
#define REF_PI_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_PI_Q15_KP               ((bm_algo_q15_t)26214)
#define REF_PI_Q15_KI               ((bm_algo_q15_t)16383)
#define REF_PI_Q15_DT               ((bm_algo_q15_t)328)
#define REF_PI_Q15_ERROR            BM_ALGO_Q15_ONE
#define REF_PI_Q15_OUT_MIN          ((bm_algo_q15_t)-32768)
#define REF_PI_Q15_OUT_MAX          BM_ALGO_Q15_ONE
#define REF_PI_Q15_GOLDEN_COUNT     5u

/** 五步 PI 输出期望（Q15） */
static const bm_algo_q15_t ref_pi_q15_golden[REF_PI_Q15_GOLDEN_COUNT] = {
    (bm_algo_q15_t)26376,
    (bm_algo_q15_t)26539,
    (bm_algo_q15_t)26702,
    (bm_algo_q15_t)26865,
    (bm_algo_q15_t)27028
};

#define REF_PI_Q15_TOLERANCE        ((bm_algo_q15_t)3)

#endif /* REF_PI_Q15_H */

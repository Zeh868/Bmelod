/**
 * @file ref_dob_q15.h
 * @brief Q15 扰动观测器（DOB）黄金参考向量
 *
 * plant_gain=0.5、lpf_alpha=0.5；单步 u=1.0、y=0.8（Q15 归一化）。
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
#ifndef REF_DOB_Q15_H
#define REF_DOB_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_DOB_Q15_PLANT_GAIN      ((bm_algo_q15_t)16383)
#define REF_DOB_Q15_LPF_ALPHA       ((bm_algo_q15_t)16383)
#define REF_DOB_Q15_U               BM_ALGO_Q15_ONE
#define REF_DOB_Q15_Y               ((bm_algo_q15_t)26214)

/** 首步扰动估计（Q15，alpha=0.5 时约为 0.15） */
#define REF_DOB_Q15_EXPECTED        ((bm_algo_q15_t)4915)
#define REF_DOB_Q15_TOLERANCE       ((bm_algo_q15_t)50)

#endif /* REF_DOB_Q15_H */

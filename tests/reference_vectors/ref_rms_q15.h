/**
 * @file ref_rms_q15.h
 * @brief Q15 滑动 RMS 黄金参考向量
 *
 * 窗口 4、恒定输入 Q15 满幅，期望 RMS ≈ 32767。
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
#ifndef REF_RMS_Q15_H
#define REF_RMS_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_RMS_Q15_WINDOW_SIZE      4u
#define REF_RMS_Q15_STEP_INPUT       BM_ALGO_Q15_ONE
#define REF_RMS_Q15_WARMUP_STEPS     4u
#define REF_RMS_Q15_EXPECTED         BM_ALGO_Q15_ONE
#define REF_RMS_Q15_TOLERANCE        ((bm_algo_q15_t)200)

#endif /* REF_RMS_Q15_H */

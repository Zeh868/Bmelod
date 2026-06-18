/**
 * @file ref_pid2_q15.h
 * @brief Q15 PID2 单步输出黄金参考
 *
 * ref=0.5、meas=0.2、kp=1、ki=0、b=1、dt=0.01 → 输出约 0.3。
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
#ifndef REF_PID2_Q15_H
#define REF_PID2_Q15_H

#include "bm/algorithm/bm_algo_fixed.h"

#define REF_PID2_Q15_REFERENCE   ((bm_algo_q15_t)16384)
#define REF_PID2_Q15_MEASUREMENT ((bm_algo_q15_t)6553)
#define REF_PID2_Q15_DT          ((bm_algo_q15_t)327)
#define REF_PID2_Q15_EXPECTED    ((bm_algo_q15_t)9830)
#define REF_PID2_Q15_TOLERANCE   ((bm_algo_q15_t)200)

#endif /* REF_PID2_Q15_H */

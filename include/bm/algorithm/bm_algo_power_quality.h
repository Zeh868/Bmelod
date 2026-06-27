/**
 * @file bm_algo_power_quality.h
 * @brief 电能质量：THD 与 P/Q/S 计量
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 * 2026-06-17       1.1            zeh            Wh 积分与谐波分组
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_POWER_QUALITY_H
#define BM_ALGO_POWER_QUALITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 由谐波幅值数组计算 THD（%），harmonics[0] 为基波 */
float bm_algo_thd_percent(const float *harmonics, uint32_t count);

/** 由 V/I RMS 与相位差（rad）计算有功/无功/视在功率 */
void bm_algo_power_quality_pq(float v_rms,
                              float i_rms,
                              float phase_rad,
                              float *p_active,
                              float *q_reactive,
                              float *s_apparent);

typedef struct {
    float accumulated_wh;
} bm_algo_energy_wh_state_t;

void bm_algo_energy_wh_reset(bm_algo_energy_wh_state_t *state);

/**
 * @brief 有功电能 Wh 积分
 *
 * @param state 积分状态
 * @param p_watts 有功功率（W）
 * @param dt_s 步长（秒）
 * @return 累计 Wh
 */
float bm_algo_energy_wh_integrator_step(bm_algo_energy_wh_state_t *state,
                                        float p_watts,
                                        float dt_s);

/**
 * @brief FFT bin → 谐波组号（基波 bin 与组宽辅助）
 *
 * @param bin 当前 bin 索引
 * @param fundamental_bin 基波 bin
 * @param group_width 每组谐波阶次宽度（>=1）
 * @return 组号（bin 0 返回 0）
 */
uint32_t bm_algo_harmonic_group_index(uint32_t bin,
                                      uint32_t fundamental_bin,
                                      uint32_t group_width);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_POWER_QUALITY_H */

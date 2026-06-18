/**
 * @file bm_algo_statistics.h
 * @brief 统计量：均值、方差、RMS、峰值与波峰因数
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_STATISTICS_H
#define BM_ALGO_STATISTICS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sum;
    float sum_sq;
    float mean;
    float m2;
    float min_v;
    float max_v;
    uint32_t count;
} bm_algo_stats_state_t;

void bm_algo_stats_reset(bm_algo_stats_state_t *state);
void bm_algo_stats_push(bm_algo_stats_state_t *state, float value);
float bm_algo_stats_mean(const bm_algo_stats_state_t *state);
float bm_algo_stats_variance(const bm_algo_stats_state_t *state);
float bm_algo_stats_rms(const bm_algo_stats_state_t *state);
float bm_algo_stats_crest_factor(const bm_algo_stats_state_t *state);

float bm_algo_array_mean(const float *data, uint32_t n);
float bm_algo_array_rms(const float *data, uint32_t n);
float bm_algo_array_peak(const float *data, uint32_t n);

/** 一阶变化率估算（输出单位/秒） */
typedef struct {
    float prev_input;
    float rate_per_s;
} bm_algo_rate_est_state_t;

void bm_algo_rate_est_reset(bm_algo_rate_est_state_t *state, float input);
float bm_algo_rate_est_step(bm_algo_rate_est_state_t *state,
                            float input,
                            float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_STATISTICS_H */

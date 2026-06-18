/**
 * @file bm_algo_resample.h
 * @brief 重采样：抽取、线性插值
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            时钟漂移跟踪/补偿
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_RESAMPLE_H
#define BM_ALGO_RESAMPLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 整数抽取（每 decim 取 1 点） */
typedef struct {
    uint32_t decim;
    uint32_t counter;
} bm_algo_decimator_state_t;

void bm_algo_decimator_reset(bm_algo_decimator_state_t *state);
int bm_algo_decimator_step(bm_algo_decimator_state_t *state,
                           uint32_t decim,
                           float input,
                           float *output);

/** 线性插值重采样（固定比率） */
typedef struct {
    float ratio;       /**< 输出/输入采样率 */
    float phase;
    float prev_sample;
} bm_algo_linear_resampler_state_t;

void bm_algo_linear_resampler_reset(bm_algo_linear_resampler_state_t *state,
                                    float ratio,
                                    float initial);
/** Returns -1 without consuming input when max_outputs is too small. */
int bm_algo_linear_resampler_step(bm_algo_linear_resampler_state_t *state,
                                  float input,
                                  float *outputs,
                                  uint32_t max_outputs,
                                  uint32_t *out_count);

/* ---------- 多相 FIR 抽取 ---------- */
typedef struct {
    const float *coeffs;
    uint32_t tap_count;
    uint32_t decim;
} bm_algo_polyphase_decim_config_t;

typedef struct {
    float *delay_line;
    uint32_t delay_len;
    uint32_t tap_count;
    uint32_t decim;
    uint32_t index;
    uint32_t decim_counter;
} bm_algo_polyphase_decim_state_t;

int bm_algo_polyphase_decim_init(bm_algo_polyphase_decim_state_t *state,
                                 const bm_algo_polyphase_decim_config_t *config,
                                 float *delay_line,
                                 uint32_t delay_len);
void bm_algo_polyphase_decim_reset(bm_algo_polyphase_decim_state_t *state,
                                   const bm_algo_polyphase_decim_config_t *config);
uint32_t bm_algo_polyphase_decim_process(bm_algo_polyphase_decim_state_t *state,
                                         const bm_algo_polyphase_decim_config_t *config,
                                         const float *in,
                                         float *out,
                                         uint32_t in_count);

/* ---------- 时钟漂移跟踪（alpha 滤波偏差估计） ---------- */
typedef struct {
    float alpha;
} bm_algo_clock_drift_config_t;

typedef struct {
    float ratio_error;
} bm_algo_clock_drift_state_t;

void bm_algo_clock_drift_reset(bm_algo_clock_drift_state_t *state);

/**
 * @brief 用期望/实际间隔更新漂移比估计
 *
 * @param state 漂移状态
 * @param config alpha 滤波系数（0,1]
 * @param expected_dt_s 标称间隔（秒）
 * @param actual_dt_s 实测间隔（秒）
 */
void bm_algo_clock_drift_feed(bm_algo_clock_drift_state_t *state,
                              const bm_algo_clock_drift_config_t *config,
                              float expected_dt_s,
                              float actual_dt_s);

/**
 * @brief 补偿实测间隔
 *
 * @param state 漂移状态（可为 NULL，返回 actual_dt_s）
 * @param actual_dt_s 实测间隔（秒）
 * @return 校正后间隔（秒）
 */
float bm_algo_clock_drift_compensate(const bm_algo_clock_drift_state_t *state,
                                     float actual_dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_RESAMPLE_H */

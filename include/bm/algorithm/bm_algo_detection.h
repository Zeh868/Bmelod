/**
 * @file bm_algo_detection.h
 * @brief 检测算法：匹配滤波与同步检波
 *
 * 用于 DTMF、超声回波与仪器弱信号捕获。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始版本
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_DETECTION_H
#define BM_ALGO_DETECTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 匹配滤波 ---------- */

float bm_algo_matched_filter(const float *signal,
                             uint32_t signal_len,
                             const float *template,
                             uint32_t template_len,
                             uint32_t *best_index);

/* ---------- 同步检波 ---------- */

typedef struct {
    float i_accum;
    float q_accum;
    float alpha;
    uint32_t count;
} bm_algo_sync_demod_state_t;

void bm_algo_sync_demod_reset(bm_algo_sync_demod_state_t *state);
void bm_algo_sync_demod_feed(bm_algo_sync_demod_state_t *state,
                             float sample,
                             float ref_sin,
                             float ref_cos);
float bm_algo_sync_demod_magnitude(const bm_algo_sync_demod_state_t *state);

/* ---------- 超声回波 ToF（包络峰检测） ---------- */
int32_t bm_algo_ultrasonic_tof(const float *echo,
                               uint32_t n,
                               uint32_t min_delay,
                               float threshold,
                               float envelope_alpha);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_DETECTION_H */

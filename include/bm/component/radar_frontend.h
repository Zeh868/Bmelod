/**
 * @file radar_frontend.h
 * @brief 雷达前端：快时间 FFT 距离像与杂波抑制
 *
 * 对 chirp 块做 RFFT 得到距离幅度谱，简易均值相减杂波抑制后输出峰值。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#ifndef BM_RADAR_FRONTEND_H
#define BM_RADAR_FRONTEND_H

#include "bm/algorithm/bm_algo_fft.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t fft_size;
    float    sample_hz;
    float    clutter_alpha;
    float    range_scale_m;
} bm_radar_frontend_config_t;

typedef struct {
    uint32_t sequence;
    uint32_t peak_bin;
    float    peak_magnitude;
    float    peak_range_m;
} bm_radar_frontend_telemetry_t;

typedef struct {
    bm_algo_rfft_f32_t fft;
    float             *profile;
    uint32_t           profile_len;
    float             *clutter_mean;
    uint32_t           step_count;
    bm_radar_frontend_telemetry_t telemetry;
} bm_radar_frontend_state_t;

typedef struct {
    bm_radar_frontend_config_t config;
    bm_radar_frontend_state_t  state;
} bm_radar_frontend_axis_t;

int  bm_radar_frontend_validate_config(
    const bm_radar_frontend_config_t *config);
int  bm_radar_frontend_init(bm_radar_frontend_axis_t *axis,
                            float *profile,
                            uint32_t profile_len,
                            float *clutter_mean,
                            float *fft_work,
                            uint32_t fft_work_count);
void bm_radar_frontend_reset(bm_radar_frontend_axis_t *axis);
int  bm_radar_frontend_feed_chirp(bm_radar_frontend_axis_t *axis,
                                  const float *chirp_samples,
                                  uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* BM_RADAR_FRONTEND_H */

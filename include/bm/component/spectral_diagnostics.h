/**
 * @file spectral_diagnostics.h
 * @brief 振动频谱诊断（Goertzel / STFT / 阶次跟踪）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 */
#ifndef BM_SPECTRAL_DIAGNOSTICS_H
#define BM_SPECTRAL_DIAGNOSTICS_H

#include "bm/algorithm/bm_algo_spectral.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_SPECTRAL_DIAG_TEL_VALID        (1u << 0u)
#define BM_SPECTRAL_DIAG_TEL_STALE        (1u << 1u)
#define BM_SPECTRAL_DIAG_TEL_ACCUMULATING (1u << 2u)

#define BM_SPECTRAL_DIAG_MAX_BINS  64u

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    goertzel_mag;
    float    order;
    float    shaft_rpm;
} bm_spectral_diagnostics_telemetry_t;

typedef int (*bm_spectral_diagnostics_feed_fn)(void *user, float *sample);

typedef void (*bm_spectral_diagnostics_publish_fn)(
    void *user,
    const bm_spectral_diagnostics_telemetry_t *telemetry);

typedef struct {
    bm_spectral_diagnostics_feed_fn    feed_sample;
    void                              *feed_sample_user;
    bm_spectral_diagnostics_publish_fn publish_telemetry;
    void                              *publish_telemetry_user;
} bm_spectral_diagnostics_resources_t;

typedef enum {
    BM_SPECTRAL_MODE_GOERTZEL = 0,
    BM_SPECTRAL_MODE_STFT
} bm_spectral_diagnostics_mode_t;

typedef struct {
    bm_spectral_diagnostics_mode_t mode;
    bm_algo_goertzel_config_t      goertzel;
    float                          sample_hz;
    uint32_t                       stft_frame_size;
    float                          pole_pairs;
    float                         *stft_frame;
    float                         *stft_window;
    float                         *stft_magnitude;
} bm_spectral_diagnostics_config_t;

typedef struct {
    bm_algo_goertzel_state_t goertzel;
    uint32_t                 frame_fill;
    uint32_t                 step_count;
    float                    goertzel_mag;
    float                    order;
    bm_spectral_diagnostics_telemetry_t telemetry;
} bm_spectral_diagnostics_state_t;

typedef struct {
    bm_spectral_diagnostics_config_t    config;
    bm_spectral_diagnostics_resources_t resources;
    bm_spectral_diagnostics_state_t     state;
} bm_spectral_diagnostics_axis_t;

int  bm_spectral_diagnostics_validate_config(
    const bm_spectral_diagnostics_config_t *config);
int  bm_spectral_diagnostics_init(bm_spectral_diagnostics_axis_t *axis);
void bm_spectral_diagnostics_reset(bm_spectral_diagnostics_axis_t *axis);
void bm_spectral_diagnostics_step(bm_spectral_diagnostics_axis_t *axis,
                                  float shaft_rpm);

#ifdef __cplusplus
}
#endif

#endif /* BM_SPECTRAL_DIAGNOSTICS_H */

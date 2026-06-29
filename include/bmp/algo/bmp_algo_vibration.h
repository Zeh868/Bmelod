/**
 * @file bmp_algo_vibration.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief K2 · 闭源 · 需 bm_mp 的振动诊断：RMS + Goertzel + 故障分数
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 */
#ifndef BMP_ALGO_VIBRATION_H
#define BMP_ALGO_VIBRATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMP_VIB_MAX_BLOCK  64u

typedef struct {
    float    sample_hz;
    float    target_freq_hz;
    uint32_t block_size;
} bmp_vib_config_t;

typedef struct {
    float rms;
    float tone_mag;
    float fault_score;
} bmp_vib_result_t;

typedef struct {
    bmp_vib_config_t cfg;
    uint8_t          initialized;
    uint8_t          reserved[3];
} bmp_vib_state_t;

int bmp_vib_diag_init(bmp_vib_state_t *state, const bmp_vib_config_t *config);

int bmp_vib_diag_step(bmp_vib_state_t *state,
                      const float *samples,
                      uint32_t sample_count,
                      bmp_vib_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_VIBRATION_H */

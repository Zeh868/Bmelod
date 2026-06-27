/**
 * @file bmp_algo_audio.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief K2 · 闭源 · 需 bm_mp 的音频 AGC 单段处理链
 */
#ifndef BMP_ALGO_AUDIO_H
#define BMP_ALGO_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMP_AUDIO_MAX_BLOCK  64u

typedef struct {
    float target_level;
    float sample_hz;
} bmp_audio_config_t;

typedef struct {
    float gain;
    uint8_t initialized;
    uint8_t reserved[3];
} bmp_audio_state_t;

int bmp_audio_agc_init(bmp_audio_state_t *state, const bmp_audio_config_t *config);

int bmp_audio_agc_step(bmp_audio_state_t *state,
                       const bmp_audio_config_t *config,
                       const float *in,
                       float *out,
                       uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_AUDIO_H */

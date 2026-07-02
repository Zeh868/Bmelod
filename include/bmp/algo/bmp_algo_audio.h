/**
 * @file bmp_algo_audio.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief K2 · 闭源 · 需 bm_mp 的音频 AGC 单段处理链
 */
#ifndef BMP_ALGO_AUDIO_H
#define BMP_ALGO_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMP_AUDIO_MAX_BLOCK  64u

/**
 * @brief 音频 AGC 配置
 *
 * 下列可调项为 0（或非正）时按内部默认值处理，保证只填 target_level/sample_hz
 * 的旧调用行为不变。
 */
typedef struct {
    float target_level;       /**< 目标输出电平 */
    float sample_hz;          /**< 采样率（Hz，必须 > 0） */
    float attack_coeff;       /**< 起控系数，0=默认 0.1 */
    float release_coeff;      /**< 释放系数，0=默认 0.01 */
    float min_gain;           /**< 增益下限，0=默认 0.01 */
    float max_gain;           /**< 增益上限，0=默认 64.0 */
    float silence_threshold;  /**< 静音门限，0=默认 1e-6 */
    float fast_attack_ms;     /**< 短块快速起控阈值（ms），0=默认 5.0 */
    float fast_attack_coeff;  /**< 短块快速起控系数，0=默认 0.3 */
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

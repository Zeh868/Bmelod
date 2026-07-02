/**
 * @file bmp_algo_audio.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief 音频 AGC 实现
 */
#include "bmp/algo/bmp_algo_audio.h"

#include "bm/algorithm/bm_algo_audio.h"

#include <string.h>

/* AGC 默认调参（config 对应字段为 0/非正时使用，保证零行为变更） */
#define BMP_AUDIO_ATTACK_COEFF_DEFAULT   0.1f
#define BMP_AUDIO_RELEASE_COEFF_DEFAULT  0.01f
#define BMP_AUDIO_MIN_GAIN_DEFAULT       0.01f
#define BMP_AUDIO_MAX_GAIN_DEFAULT       64.0f
#define BMP_AUDIO_SILENCE_THRESH_DEFAULT 1e-6f
#define BMP_AUDIO_FAST_ATTACK_MS_DEFAULT 5.0f
#define BMP_AUDIO_FAST_ATTACK_COEFF_DEFAULT 0.3f

/** @brief 取正配置值，非正时回退默认值 */
static float bmp_audio_cfg_or(float cfg_val, float def_val) {
    return (cfg_val > 0.0f) ? cfg_val : def_val;
}

int bmp_audio_agc_init(bmp_audio_state_t *state, const bmp_audio_config_t *config) {
    if (state == NULL || config == NULL || config->sample_hz <= 0.0f) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    state->gain = 1.0f;
    state->initialized = 1u;
    return 0;
}

int bmp_audio_agc_step(bmp_audio_state_t *state,
                       const bmp_audio_config_t *config,
                       const float *in,
                       float *out,
                       uint32_t sample_count) {
    bm_algo_agc_config_t agc_cfg;
    bm_algo_agc_state_t agc_st;
    float block_ms;
    float fast_attack_ms;

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        state->initialized == 0u || sample_count == 0u ||
        sample_count > BMP_AUDIO_MAX_BLOCK) {
        return -1;
    }
    block_ms = ((float)sample_count * 1000.0f) / config->sample_hz;
    fast_attack_ms = bmp_audio_cfg_or(config->fast_attack_ms,
                                      BMP_AUDIO_FAST_ATTACK_MS_DEFAULT);
    agc_cfg.target_level = config->target_level;
    agc_cfg.attack_coeff = bmp_audio_cfg_or(config->attack_coeff,
                                            BMP_AUDIO_ATTACK_COEFF_DEFAULT);
    agc_cfg.release_coeff = bmp_audio_cfg_or(config->release_coeff,
                                             BMP_AUDIO_RELEASE_COEFF_DEFAULT);
    agc_cfg.gain = state->gain;
    agc_cfg.min_gain = bmp_audio_cfg_or(config->min_gain,
                                        BMP_AUDIO_MIN_GAIN_DEFAULT);
    agc_cfg.max_gain = bmp_audio_cfg_or(config->max_gain,
                                        BMP_AUDIO_MAX_GAIN_DEFAULT);
    agc_cfg.silence_threshold = bmp_audio_cfg_or(config->silence_threshold,
                                                 BMP_AUDIO_SILENCE_THRESH_DEFAULT);
    if (block_ms < fast_attack_ms) {
        agc_cfg.attack_coeff = bmp_audio_cfg_or(config->fast_attack_coeff,
                                                BMP_AUDIO_FAST_ATTACK_COEFF_DEFAULT);
    }
    agc_st.gain = state->gain;
    bm_algo_agc_process(&agc_st, &agc_cfg, in, out, sample_count);
    state->gain = agc_st.gain;
    return 0;
}

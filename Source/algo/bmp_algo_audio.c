/**
 * @file bmp_algo_audio.c
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief 音频 AGC 实现
 */
#include "bmp/algo/bmp_algo_audio.h"

#include "bm/algorithm/bm_algo_audio.h"

#include <string.h>

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

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        state->initialized == 0u || sample_count == 0u ||
        sample_count > BMP_AUDIO_MAX_BLOCK) {
        return -1;
    }
    block_ms = ((float)sample_count * 1000.0f) / config->sample_hz;
    agc_cfg.target_level = config->target_level;
    agc_cfg.attack_coeff = 0.1f;
    agc_cfg.release_coeff = 0.01f;
    agc_cfg.gain = state->gain;
    agc_cfg.min_gain = 0.01f;
    agc_cfg.max_gain = 64.0f;
    agc_cfg.silence_threshold = 1e-6f;
    if (block_ms < 5.0f) {
        agc_cfg.attack_coeff = 0.3f;
    }
    agc_st.gain = state->gain;
    bm_algo_agc_process(&agc_st, &agc_cfg, in, out, sample_count);
    state->gain = agc_st.gain;
    return 0;
}

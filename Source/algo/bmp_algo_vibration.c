/**
 * @file bmp_algo_vibration.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief 振动诊断实现
 */
#include "bmp/algo/bmp_algo_vibration.h"

#include "bm/algorithm/bm_algo_spectral.h"
#include "bm/algorithm/bm_algo_statistics.h"

#include <string.h>

int bmp_vib_diag_init(bmp_vib_state_t *state, const bmp_vib_config_t *config) {
    if (state == NULL || config == NULL || config->block_size == 0u ||
        config->block_size > BMP_VIB_MAX_BLOCK ||
        config->sample_hz <= 0.0f || config->target_freq_hz <= 0.0f) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    state->cfg = *config;
    state->initialized = 1u;
    return 0;
}

int bmp_vib_diag_step(bmp_vib_state_t *state,
                      const float *samples,
                      uint32_t sample_count,
                      bmp_vib_result_t *out) {
    bm_algo_goertzel_config_t gz_cfg;
    bm_algo_goertzel_state_t gz_st;
    uint32_t i;
    float rms;
    float tone_mag;
    float fault;

    if (state == NULL || samples == NULL || out == NULL ||
        state->initialized == 0u) {
        return -1;
    }
    if (sample_count != state->cfg.block_size) {
        return -2;
    }
    rms = bm_algo_array_rms(samples, sample_count);
    gz_cfg.target_freq_hz = state->cfg.target_freq_hz;
    gz_cfg.sample_hz = state->cfg.sample_hz;
    gz_cfg.block_size = sample_count;
    gz_cfg.coeff = 0.0f;
    if (bm_algo_goertzel_init(&gz_st, &gz_cfg) != 0) {
        return -3;
    }
    for (i = 0u; i < sample_count; i++) {
        (void)bm_algo_goertzel_feed(&gz_st, &gz_cfg, samples[i]);
    }
    tone_mag = bm_algo_goertzel_result(&gz_st, &gz_cfg);
    fault = tone_mag / (rms + 1e-6f);
    if (fault > 1.0f) {
        fault = 1.0f;
    }
    out->rms = rms;
    out->tone_mag = tone_mag;
    out->fault_score = fault;
    return 0;
}

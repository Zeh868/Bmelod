/**
 * @file bmp_algo_fft.c
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief 增强 FFT 实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 */
#include "bmp/algo/bmp_algo_fft.h"

#include "bm/algorithm/bm_algo_fft.h"
#include "bm/algorithm/bm_algo_spectral.h"

#include <string.h>

int bmp_fft_enhanced_init(bmp_fft_state_t *state, const bmp_fft_config_t *config) {
    bm_algo_rfft_f32_t rfft;
    uint32_t size;

    if (state == NULL || config == NULL) {
        return -1;
    }
    size = config->fft_size;
    if (size == 0u || size > BMP_FFT_MAX_SIZE ||
        bm_algo_fft_is_supported_size(size) == 0) {
        return -2;
    }
    memset(state, 0, sizeof(*state));
    state->fft_size = size;
    state->sample_hz = config->sample_hz;
    bm_algo_window_hann(state->window, size);
    rfft.size = size;
    rfft.work = state->work;
    rfft.work_count = size * 2u;
    rfft.twiddle = NULL;
    rfft.twiddle_count = 0u;
    if (bm_algo_rfft_f32_init(&rfft, size, state->work,
                              (uint32_t)(sizeof(state->work) /
                                         sizeof(state->work[0]))) != 0) {
        return -3;
    }
    state->initialized = 1u;
    return 0;
}

int bmp_fft_enhanced_step(bmp_fft_state_t *state,
                          const float *samples,
                          uint32_t sample_count,
                          bmp_fft_result_t *out) {
    bm_algo_rfft_f32_t rfft;
    uint32_t i;
    uint32_t size;

    if (state == NULL || samples == NULL || out == NULL ||
        state->initialized == 0u) {
        return -1;
    }
    size = state->fft_size;
    if (sample_count < size) {
        return -2;
    }
    for (i = 0u; i < size; i++) {
        state->windowed[i] = samples[i] * state->window[i];
    }
    /*
     * P2-10：init 已校验 size/work 并填好实例，step 直接复用同布局无状态实例，
     * 不再重复 bm_algo_rfft_f32_init（其仅做校验+赋同值字段，无预计算/副作用）。
     */
    rfft.size = size;
    rfft.work = state->work;
    rfft.work_count = size * 2u;
    rfft.twiddle = NULL;
    rfft.twiddle_count = 0u;
    if (bm_algo_rfft_f32_execute(&rfft, state->windowed, state->spectrum) != 0) {
        return -4;
    }
    if (bm_algo_find_peak_bin(state->spectrum, 1u, size / 2u,
                              &out->peak_bin, &out->peak_mag) != 0) {
        out->peak_bin = 0u;
        out->peak_mag = 0.0f;
        return -5;
    }
    return 0;
}

/**
 * @file bm_algo_spectral.c
 * @brief 频谱分析算法实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 STFT 幅度谱与阶次换算
 * 2026-06-17       1.2            zeh            增加重叠 STFT 状态机
 * 2026-06-23       1.3            zeh            bm_algo_stft_overlap_init 增加 frame_size>64 上限校验，与 feed 内栈帧约束一致
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_spectral.h"
#include "bm/algorithm/bm_algo_errors.h"
#include <stddef.h>

#include <math.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

/** 重叠 STFT 单帧样本硬上限（栈数组 frame_stack 尺寸，与 feed 内约束一致） */
#define BM_ALGO_STFT_MAX_FRAME 64u

/** 包络跟踪器默认一阶 LPF 系数（未配置时的默认平滑因子） */
#define BM_ALGO_ENVELOPE_ALPHA_DEFAULT 0.1f

int bm_algo_goertzel_init(bm_algo_goertzel_state_t *state,
                          const bm_algo_goertzel_config_t *config) {
    float omega;

    if (state == NULL || config == NULL ||
        config->block_size == 0u || config->sample_hz <= 0.0f ||
        config->target_freq_hz < 0.0f ||
        config->target_freq_hz > 0.5f * config->sample_hz) {
        return BM_ALGO_ERR_INVALID;
    }

    omega = 2.0f * BM_ALGO_PI_F * config->target_freq_hz / config->sample_hz;
    bm_algo_goertzel_reset(state);
    state->coeff = 2.0f * cosf(omega);
    return 0;
}

void bm_algo_goertzel_reset(bm_algo_goertzel_state_t *state) {
    if (state != NULL) {
        state->s_prev = 0.0f;
        state->s_prev2 = 0.0f;
        state->count = 0u;
    }
}

int bm_algo_goertzel_feed(bm_algo_goertzel_state_t *state,
                          const bm_algo_goertzel_config_t *config,
                          float sample) {
    float s;

    if (state == NULL || config == NULL) {
        return BM_ALGO_ERR_INVALID;
    }

    s = sample + state->coeff * state->s_prev - state->s_prev2;
    state->s_prev2 = state->s_prev;
    state->s_prev = s;
    state->count++;

    return (state->count >= config->block_size) ? 1 : 0;
}

float bm_algo_goertzel_result(bm_algo_goertzel_state_t *state,
                              const bm_algo_goertzel_config_t *config) {
    float real;
    float imag;
    float power;

    if (state == NULL || config == NULL) {
        return 0.0f;
    }

    real = state->s_prev - state->s_prev2 * cosf(
        2.0f * BM_ALGO_PI_F * config->target_freq_hz / config->sample_hz);
    imag = state->s_prev2 * sinf(
        2.0f * BM_ALGO_PI_F * config->target_freq_hz / config->sample_hz);
    power = real * real + imag * imag;
    return sqrtf(power / (float)config->block_size);
}

void bm_algo_psd_from_spectrum(const float *mag,
                               uint32_t bin_count,
                               float scale,
                               float *psd) {
    uint32_t i;

    if (mag == NULL || psd == NULL) {
        return;
    }

    for (i = 0u; i < bin_count; ++i) {
        psd[i] = mag[i] * mag[i] * scale;
    }
}

void bm_algo_envelope_reset(bm_algo_envelope_state_t *state) {
    if (state != NULL) {
        state->prev = 0.0f;
        state->envelope = 0.0f;
        state->alpha = BM_ALGO_ENVELOPE_ALPHA_DEFAULT;
    }
}

float bm_algo_envelope_step(bm_algo_envelope_state_t *state, float input) {
    float abs_in;

    if (state == NULL) {
        return input;
    }

    abs_in = fabsf(input);
    state->envelope += state->alpha * (abs_in - state->envelope);
    state->prev = input;
    return state->envelope;
}

float bm_algo_correlate(const float *a, const float *b, uint32_t len) {
    float sum = 0.0f;
    uint32_t i;

    if (a == NULL || b == NULL) {
        return 0.0f;
    }

    for (i = 0u; i < len; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

int bm_algo_find_peak_bin(const float *spectrum,
                          uint32_t start_bin,
                          uint32_t end_bin,
                          uint32_t *peak_bin,
                          float *peak_value) {
    uint32_t i;
    float max_v = -1.0f;
    uint32_t max_i = start_bin;

    if (spectrum == NULL || peak_bin == NULL || peak_value == NULL ||
        end_bin <= start_bin) {
        return BM_ALGO_ERR_INVALID;
    }

    for (i = start_bin; i < end_bin; ++i) {
        if (spectrum[i] > max_v) {
            max_v = spectrum[i];
            max_i = i;
        }
    }

    *peak_bin = max_i;
    *peak_value = max_v;
    return 0;
}

static void dft_magnitude(const float *frame,
                          const float *window,
                          uint32_t n,
                          float *magnitude) {
    uint32_t k;
    uint32_t bin_count = n / 2u + 1u;

    for (k = 0u; k < bin_count; ++k) {
        float re = 0.0f;
        float im = 0.0f;
        uint32_t i;
        float omega = 2.0f * BM_ALGO_PI_F * (float)k / (float)n;

        for (i = 0u; i < n; ++i) {
            float x = frame[i];
            float w = (window != NULL) ? window[i] : 1.0f;
            float sample = x * w;

            re += sample * cosf(omega * (float)i);
            im -= sample * sinf(omega * (float)i);
        }

        magnitude[k] = sqrtf(re * re + im * im) / (float)n;
    }
}

int bm_algo_stft_magnitude_frame(const float *frame,
                                 const float *window,
                                 uint32_t n,
                                 float *magnitude) {
    if (frame == NULL || magnitude == NULL || n < 2u) {
        return BM_ALGO_ERR_INVALID;
    }

    dft_magnitude(frame, window, n, magnitude);
    return 0;
}

/**
 * @brief 从环形缓冲按时间顺序提取一帧
 */
static void stft_overlap_extract_frame(const bm_algo_stft_overlap_t *state,
                                       float *frame) {
    uint32_t i;
    uint32_t idx;

    for (i = 0u; i < state->frame_size; ++i) {
        idx = (state->write_idx + i) % state->frame_size;
        frame[i] = state->ring_buffer[idx];
    }
}

int bm_algo_stft_overlap_init(bm_algo_stft_overlap_t *state,
                              const bm_algo_stft_overlap_config_t *config,
                              float *ring_buffer,
                              uint32_t ring_buffer_len) {
    if (state == NULL || config == NULL || ring_buffer == NULL ||
        config->frame_size < 2u || config->frame_size > BM_ALGO_STFT_MAX_FRAME ||
        config->hop_size == 0u ||
        config->hop_size > config->frame_size ||
        ring_buffer_len < config->frame_size) {
        return BM_ALGO_ERR_INVALID;
    }
    state->frame_size = config->frame_size;
    state->hop_size = config->hop_size;
    state->ring_buffer = ring_buffer;
    bm_algo_stft_overlap_reset(state);
    return 0;
}

void bm_algo_stft_overlap_reset(bm_algo_stft_overlap_t *state) {
    uint32_t i;

    if (state == NULL) {
        return;
    }
    if (state->ring_buffer != NULL && state->frame_size > 0u) {
        for (i = 0u; i < state->frame_size; ++i) {
            state->ring_buffer[i] = 0.0f;
        }
    }
    state->write_idx = 0u;
    state->filled = 0u;
    state->samples_since_hop = 0u;
    state->frame_count = 0u;
}

int bm_algo_stft_overlap_feed(bm_algo_stft_overlap_t *state,
                              const bm_algo_stft_overlap_config_t *config,
                              float sample,
                              float *magnitude_out,
                              uint32_t magnitude_len) {
    uint32_t need_bins;

    if (state == NULL || config == NULL || state->ring_buffer == NULL) {
        return BM_ALGO_ERR_INVALID;
    }
    need_bins = state->frame_size / 2u + 1u;
    if (magnitude_out == NULL || magnitude_len < need_bins) {
        return BM_ALGO_ERR_INVALID;
    }

    state->ring_buffer[state->write_idx] = sample;
    state->write_idx = (state->write_idx + 1u) % state->frame_size;
    if (state->filled < state->frame_size) {
        state->filled++;
    }
    state->samples_since_hop++;

    if (state->filled < state->frame_size) {
        return 0;
    }
    if (state->samples_since_hop < state->hop_size) {
        return 0;
    }

    {
        float frame_stack[BM_ALGO_STFT_MAX_FRAME];

        if (state->frame_size > BM_ALGO_STFT_MAX_FRAME) {
            /* frame_size 超出静态栈帧缓冲 frame_stack 的容量上限。 */
            return BM_ALGO_ERR_OVERFLOW;
        }
        stft_overlap_extract_frame(state, frame_stack);
        if (bm_algo_stft_magnitude_frame(frame_stack, config->window,
                                         state->frame_size,
                                         magnitude_out) != 0) {
            return BM_ALGO_ERR_INVALID;
        }
    }

    state->samples_since_hop = 0u;
    state->frame_count++;
    return 1;
}

float bm_algo_order_from_hz(float freq_hz, float rpm, float pole_pairs_or_harmonic) {
    float shaft_hz;

    if (rpm <= 0.0f || pole_pairs_or_harmonic <= 0.0f) {
        return 0.0f;
    }

    shaft_hz = (rpm / 60.0f) * pole_pairs_or_harmonic;
    if (shaft_hz <= 0.0f) {
        return 0.0f;
    }
    return freq_hz / shaft_hz;
}

void bm_algo_order_tracker_reset(bm_algo_order_tracker_state_t *state) {
    if (state != NULL) {
        state->filtered_order = 0.0f;
        state->shaft_hz = 0.0f;
    }
}

void bm_algo_order_tracker_feed(bm_algo_order_tracker_state_t *state,
                                const bm_algo_order_tracker_config_t *config,
                                float rpm_hint,
                                float peak_freq_hz) {
    float shaft_hz;
    float raw_order;
    float alpha;

    if (state == NULL || config == NULL) {
        return;
    }

    shaft_hz = (rpm_hint / 60.0f) * config->pole_pairs;
    if (shaft_hz <= 0.0f) {
        return;
    }

    state->shaft_hz = shaft_hz;
    if (peak_freq_hz > 0.0f) {
        raw_order = peak_freq_hz / shaft_hz;
    } else {
        raw_order = 1.0f;
    }

    alpha = config->lpf_alpha;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    state->filtered_order = alpha * raw_order +
                            (1.0f - alpha) * state->filtered_order;
}

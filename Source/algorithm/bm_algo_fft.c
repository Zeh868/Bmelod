/**
 * @file bm_algo_fft.c
 * @brief radix-2 FFT（CFFT/RFFT）与窗函数实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_fft.h"
#include <stddef.h>

#include <math.h>
#include <string.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

int bm_algo_fft_is_supported_size(uint32_t size) {
    switch (size) {
    case BM_ALGO_FFT_SIZE_64:
    case BM_ALGO_FFT_SIZE_128:
    case BM_ALGO_FFT_SIZE_256:
    case BM_ALGO_FFT_SIZE_512:
    case BM_ALGO_FFT_SIZE_1024:
        return 1;
    default:
        return 0;
    }
}

static uint32_t bit_reverse(uint32_t x, uint32_t bits) {
    uint32_t y = 0u;
    uint32_t i;

    for (i = 0u; i < bits; ++i) {
        y = (y << 1u) | (x & 1u);
        x >>= 1u;
    }
    return y;
}

static void fft_bit_reverse(float *data, uint32_t n) {
    uint32_t bits = 0u;
    uint32_t i;
    uint32_t j;
    uint32_t m = n;

    while (m > 1u) {
        bits++;
        m >>= 1u;
    }

    for (i = 0u; i < n; ++i) {
        j = bit_reverse(i, bits);
        if (j > i) {
            float tr = data[2u * i];
            float ti = data[2u * i + 1u];
            data[2u * i] = data[2u * j];
            data[2u * i + 1u] = data[2u * j + 1u];
            data[2u * j] = tr;
            data[2u * j + 1u] = ti;
        }
    }
}

static void fft_radix2(float *data, uint32_t n, int inverse) {
    uint32_t stage;
    uint32_t m;
    uint32_t k;
    uint32_t i;
    float sign = inverse ? 1.0f : -1.0f;

    fft_bit_reverse(data, n);

    for (stage = 1u; (1u << stage) <= n; ++stage) {
        m = 1u << stage;
        for (k = 0u; k < n; k += m) {
            for (i = 0u; i < m / 2u; ++i) {
                float theta = sign * 2.0f * BM_ALGO_PI_F * (float)i / (float)m;
                float wr = cosf(theta);
                float wi = sinf(theta);
                uint32_t idx_even = k + i;
                uint32_t idx_odd = k + i + m / 2u;
                float tr = wr * data[2u * idx_odd] - wi * data[2u * idx_odd + 1u];
                float ti = wr * data[2u * idx_odd + 1u] + wi * data[2u * idx_odd];
                float ur = data[2u * idx_even];
                float ui = data[2u * idx_even + 1u];

                data[2u * idx_even] = ur + tr;
                data[2u * idx_even + 1u] = ui + ti;
                data[2u * idx_odd] = ur - tr;
                data[2u * idx_odd + 1u] = ui - ti;
            }
        }
    }

    if (inverse) {
        float scale = 1.0f / (float)n;
        for (i = 0u; i < 2u * n; ++i) {
            data[i] *= scale;
        }
    }
}

int bm_algo_cfft_f32_init(bm_algo_cfft_f32_t *fft,
                          uint32_t size,
                          float *work,
                          uint32_t work_count) {
    if (fft == NULL || work == NULL || !bm_algo_fft_is_supported_size(size) ||
        work_count < 2u * size) {
        return -1;
    }
    fft->size = size;
    fft->work = work;
    fft->work_count = work_count;
    fft->twiddle = NULL;
    fft->twiddle_count = 0u;
    return 0;
}

int bm_algo_cfft_f32_forward(bm_algo_cfft_f32_t *fft, float *real_imag) {
    if (fft == NULL || real_imag == NULL) {
        return -1;
    }
    fft_radix2(real_imag, fft->size, 0);
    return 0;
}

int bm_algo_cfft_f32_inverse(bm_algo_cfft_f32_t *fft, float *real_imag) {
    if (fft == NULL || real_imag == NULL) {
        return -1;
    }
    fft_radix2(real_imag, fft->size, 1);
    return 0;
}

int bm_algo_rfft_f32_init(bm_algo_rfft_f32_t *fft,
                          uint32_t size,
                          float *work,
                          uint32_t work_count) {
    return bm_algo_cfft_f32_init((bm_algo_cfft_f32_t *)fft, size, work, work_count);
}

int bm_algo_rfft_f32_execute(bm_algo_rfft_f32_t *fft,
                             const float *time_data,
                             float *spectrum_mag) {
    uint32_t i;
    bm_algo_cfft_f32_t *cfft = (bm_algo_cfft_f32_t *)fft;

    if (fft == NULL || time_data == NULL || spectrum_mag == NULL ||
        cfft->work == NULL) {
        return -1;
    }

    for (i = 0u; i < fft->size; ++i) {
        cfft->work[2u * i] = time_data[i];
        cfft->work[2u * i + 1u] = 0.0f;
    }

    fft_radix2(cfft->work, fft->size, 0);

    for (i = 0u; i <= fft->size / 2u; ++i) {
        float re = cfft->work[2u * i];
        float im = cfft->work[2u * i + 1u];
        spectrum_mag[i] = sqrtf(re * re + im * im) / (float)fft->size;
    }
    return 0;
}

void bm_algo_window_hann(float *window, uint32_t n) {
    uint32_t i;
    if (window == NULL || n == 0u) {
        return;
    }
    if (n == 1u) {
        window[0] = 1.0f;
        return;
    }
    for (i = 0u; i < n; ++i) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * BM_ALGO_PI_F * (float)i / (float)(n - 1u)));
    }
}

void bm_algo_window_hamming(float *window, uint32_t n) {
    uint32_t i;
    if (window == NULL || n == 0u) {
        return;
    }
    if (n == 1u) {
        window[0] = 1.0f;
        return;
    }
    for (i = 0u; i < n; ++i) {
        window[i] = 0.54f - 0.46f * cosf(2.0f * BM_ALGO_PI_F * (float)i / (float)(n - 1u));
    }
}

void bm_algo_window_blackman(float *window, uint32_t n) {
    uint32_t i;
    float a0 = 0.42f;
    float a1 = 0.5f;
    float a2 = 0.08f;

    if (window == NULL || n == 0u) {
        return;
    }
    if (n == 1u) {
        window[0] = 1.0f;
        return;
    }
    for (i = 0u; i < n; ++i) {
        float x = 2.0f * BM_ALGO_PI_F * (float)i / (float)(n - 1u);
        window[i] = a0 - a1 * cosf(x) + a2 * cosf(2.0f * x);
    }
}

float bm_algo_window_coherent_gain(const float *window, uint32_t n) {
    float sum = 0.0f;
    uint32_t i;

    if (window == NULL || n == 0u) {
        return 1.0f;
    }
    for (i = 0u; i < n; ++i) {
        sum += window[i];
    }
    return sum / (float)n;
}

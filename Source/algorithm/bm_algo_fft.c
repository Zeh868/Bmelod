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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_fft.h"
#include "bm/algorithm/bm_algo_errors.h"
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

/**
 * @brief 对整数 x 的低 bits 位执行位反转（比特逆序）
 *
 * 用于 radix-2 FFT 中将时域序号映射到位反转序号。
 * 例如：bits=3，x=0b001 → 返回 0b100。
 *
 * @param x    待反转的整数
 * @param bits 有效位数（即 log2(N)）
 * @return 位反转后的值
 */
static uint32_t bit_reverse(uint32_t x, uint32_t bits) {
    uint32_t y = 0u;
    uint32_t i;

    for (i = 0u; i < bits; ++i) {
        y = (y << 1u) | (x & 1u);
        x >>= 1u;
    }
    return y;
}

/**
 * @brief 对复数交错数组执行原址位反转排列
 *
 * 将 data[i] 与 data[bit_reverse(i)] 互换，为后续蝶形运算准备输入顺序。
 * data 格式为 [re0,im0,re1,im1,...,re{n-1},im{n-1}]，实际元素数为 2*n。
 *
 * @param data 复数交错缓冲（原址操作），长度 2*n
 * @param n    复数点数（须为 2 的幂）
 */
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

/**
 * @brief Cooley-Tukey radix-2 DIT FFT 核心（原址，复数交错格式）
 *
 * 算法步骤：
 *  1. 调用 fft_bit_reverse() 完成位反转排列；
 *  2. 按蝶形级数 log2(n) 迭代，每级蝶形步长 m=2^stage；
 *  3. 旋转因子 W = e^{sign*j*2π*i/m}，正变换 sign=-1，逆变换 sign=+1；
 *  4. 逆变换结束后整体乘以 1/N 完成归一化。
 *
 * @param data    复数交错缓冲（原址操作），长度 2*n
 * @param n       复数点数（须为 BM_ALGO_FFT_SIZE_* 之一）
 * @param inverse 0 正变换；1 逆变换（含 1/N 归一化）
 */
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
        return BM_ALGO_ERR_INVALID;
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
        return BM_ALGO_ERR_INVALID;
    }
    fft_radix2(real_imag, fft->size, 0);
    return 0;
}

int bm_algo_cfft_f32_inverse(bm_algo_cfft_f32_t *fft, float *real_imag) {
    if (fft == NULL || real_imag == NULL) {
        return BM_ALGO_ERR_INVALID;
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

/**
 * @brief 实数 FFT 执行：时域 → 幅值谱
 *
 * 将 time_data 零填充为复数序列存入 work，调用正向 radix-2 FFT，
 * 对 0..N/2 各频率箱计算幅值 sqrt(re²+im²)/N 并写入 spectrum_mag。
 *
 * @param fft         已初始化的 RFFT 实例
 * @param time_data   时域实数输入，长度 size
 * @param spectrum_mag 输出幅值谱，长度 size/2+1
 * @return 0 成功；-1 参数非法
 */
int bm_algo_rfft_f32_execute(bm_algo_rfft_f32_t *fft,
                             const float *time_data,
                             float *spectrum_mag) {
    uint32_t i;
    bm_algo_cfft_f32_t *cfft = (bm_algo_cfft_f32_t *)fft;

    if (fft == NULL || time_data == NULL || spectrum_mag == NULL ||
        cfft->work == NULL) {
        return BM_ALGO_ERR_INVALID;
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

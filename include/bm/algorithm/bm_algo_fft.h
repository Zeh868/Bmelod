/**
 * @file bm_algo_fft.h
 * @brief 复数/实数 FFT：radix-2，支持 64/128/256/512/1024 点
 *
 * 工作区与 twiddle 由调用方提供，禁止堆分配。
 *
 * @maturity E1
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
#ifndef BM_ALGO_FFT_H
#define BM_ALGO_FFT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_ALGO_FFT_SIZE_64    64u
#define BM_ALGO_FFT_SIZE_128   128u
#define BM_ALGO_FFT_SIZE_256   256u
#define BM_ALGO_FFT_SIZE_512   512u
#define BM_ALGO_FFT_SIZE_1024  1024u

typedef struct {
    uint32_t size;
    float *work;           /**< 长度 2*size 的复数交错缓冲 [re,im,...] */
    uint32_t work_count;
    const float *twiddle;  /**< 长度 size 的 cos/sin 表，或 NULL 用内置 */
    uint32_t twiddle_count;
} bm_algo_cfft_f32_t;

typedef struct {
    uint32_t size;
    float *work;
    uint32_t work_count;
    const float *twiddle;
    uint32_t twiddle_count;
} bm_algo_rfft_f32_t;

int bm_algo_fft_is_supported_size(uint32_t size);
int bm_algo_cfft_f32_init(bm_algo_cfft_f32_t *fft,
                          uint32_t size,
                          float *work,
                          uint32_t work_count);
int bm_algo_cfft_f32_forward(bm_algo_cfft_f32_t *fft, float *real_imag);
int bm_algo_cfft_f32_inverse(bm_algo_cfft_f32_t *fft, float *real_imag);

int bm_algo_rfft_f32_init(bm_algo_rfft_f32_t *fft,
                          uint32_t size,
                          float *work,
                          uint32_t work_count);
int bm_algo_rfft_f32_execute(bm_algo_rfft_f32_t *fft,
                             const float *time_data,
                             float *spectrum_mag);

/** 窗函数（长度 n，输出到 window[]） */
void bm_algo_window_hann(float *window, uint32_t n);
void bm_algo_window_hamming(float *window, uint32_t n);
void bm_algo_window_blackman(float *window, uint32_t n);

/** 窗相干增益（幅值修正系数） */
float bm_algo_window_coherent_gain(const float *window, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FFT_H */

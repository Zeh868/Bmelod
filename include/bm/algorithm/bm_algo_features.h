/**
 * @file bm_algo_features.h
 * @brief TinyML 前后处理：量化、反量化与 log-mel 特征
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            MFCC DCT 近似
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_FEATURES_H
#define BM_ALGO_FEATURES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int8_t bm_algo_quantize_f32_to_i8(float value, float scale, int32_t zero_point);
float bm_algo_dequantize_i8_to_f32(int8_t value, float scale, int32_t zero_point);

void bm_algo_quantize_buffer_f32_i8(const float *in,
                                    int8_t *out,
                                    uint32_t n,
                                    float scale,
                                    int32_t zero_point);

/** 简易 log-mel 能量（需预计算 mel 滤波器权重） */
void bm_algo_log_mel_energy(const float *power_spectrum,
                            const float *mel_weights,
                            uint32_t mel_bins,
                            uint32_t fft_bins,
                            float *mel_out);

typedef struct {
    uint32_t n_mfcc;
    uint32_t n_mels;
    float    log_floor;
} bm_algo_mfcc_config_t;

/**
 * @brief MFCC：log-mel + 调用者提供的 DCT 矩阵
 *
 * @param config MFCC 维度与 log 下限
 * @param log_mel log-mel 向量（长度 n_mels）
 * @param dct_matrix DCT 系数矩阵（n_mfcc × n_mels，行主序）
 * @param mfcc_out 输出 MFCC（长度 n_mfcc）
 */
void bm_algo_mfcc_compute(const bm_algo_mfcc_config_t *config,
                          const float *log_mel,
                          const float *dct_matrix,
                          float *mfcc_out);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FEATURES_H */

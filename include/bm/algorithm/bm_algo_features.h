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

/**
 * @brief 单值 float32 量化为 int8（TFLite 兼容）
 *
 * 按 q = round(value / scale + zero_point) 计算，结果截断至 [-128, 127]。
 * scale 非正或非有限时返回 0；value 无穷大时直接返回饱和值。
 *
 * @param value      待量化的浮点值
 * @param scale      量化缩放因子，须 > 0
 * @param zero_point 量化零点偏移
 * @return 量化后的 int8 值
 */
int8_t bm_algo_quantize_f32_to_i8(float value, float scale, int32_t zero_point);

/**
 * @brief 单值 int8 反量化为 float32（TFLite 兼容）
 *
 * 按 f = (value - zero_point) * scale 计算。
 *
 * @param value      待反量化的 int8 值
 * @param scale      量化缩放因子
 * @param zero_point 量化零点偏移
 * @return 反量化后的浮点值
 */
float bm_algo_dequantize_i8_to_f32(int8_t value, float scale, int32_t zero_point);

/**
 * @brief 批量 float32 量化为 int8
 *
 * 对输入数组逐元素调用 bm_algo_quantize_f32_to_i8，结果写入 out。
 *
 * @param in         输入浮点数组，长度 n
 * @param out        输出 int8 数组，长度 n，调用者分配
 * @param n          数组元素个数
 * @param scale      量化缩放因子，须 > 0
 * @param zero_point 量化零点偏移
 */
void bm_algo_quantize_buffer_f32_i8(const float *in,
                                    int8_t *out,
                                    uint32_t n,
                                    float scale,
                                    int32_t zero_point);

/**
 * @brief 简易 log-mel 滤波器组能量计算
 *
 * 对每个 Mel 滤波器，将功率谱与预计算的三角滤波器权重做点积后取自然对数，
 * 加小量 1e-10f 防止 log(0)。权重矩阵为行主序，形状 mel_bins × fft_bins。
 *
 * @param power_spectrum 输入功率谱，长度 fft_bins（FFT 幅度平方）
 * @param mel_weights    Mel 滤波器权重矩阵，行主序，大小 mel_bins × fft_bins
 * @param mel_bins       Mel 滤波器数量（输出维度）
 * @param fft_bins       FFT 频率箱数（输入维度）
 * @param mel_out        输出 log-mel 能量向量，长度 mel_bins，调用者分配
 */
void bm_algo_log_mel_energy(const float *power_spectrum,
                            const float *mel_weights,
                            uint32_t mel_bins,
                            uint32_t fft_bins,
                            float *mel_out);

/**
 * @brief MFCC 计算配置
 */
typedef struct {
    uint32_t n_mfcc;    /**< 输出 MFCC 系数个数 */
    uint32_t n_mels;    /**< Mel 滤波器数量（log-mel 输入维度） */
    float    log_floor; /**< log-mel 下限，<= 0 时使用默认值 1e-10f */
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

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

/**
 * @brief 复数 FFT 实例（工作区与旋转因子表由调用方提供）
 */
typedef struct {
    uint32_t size;              /**< FFT 点数，取值见 BM_ALGO_FFT_SIZE_* */
    float *work;                /**< 长度 2*size 的复数交错缓冲 [re,im,...] */
    uint32_t work_count;        /**< work 数组元素个数（≥2*size） */
    const float *twiddle;       /**< 长度 size 的预计算旋转因子 cos/sin 表，或 NULL 使用内置实时计算 */
    uint32_t twiddle_count;     /**< twiddle 数组元素个数 */
} bm_algo_cfft_f32_t;

/**
 * @brief 实数 FFT 实例（内部复用 cfft 布局）
 */
typedef struct {
    uint32_t size;              /**< FFT 点数，取值见 BM_ALGO_FFT_SIZE_* */
    float *work;                /**< 长度 2*size 的复数工作缓冲 */
    uint32_t work_count;        /**< work 数组元素个数（≥2*size） */
    const float *twiddle;       /**< 旋转因子表，或 NULL */
    uint32_t twiddle_count;     /**< twiddle 数组元素个数 */
} bm_algo_rfft_f32_t;

/**
 * @brief 检查 FFT 点数是否在支持范围内
 *
 * @param size 待检查的点数
 * @return 1 支持；0 不支持
 */
int bm_algo_fft_is_supported_size(uint32_t size);

/**
 * @brief 初始化复数 FFT 实例
 *
 * @param fft        FFT 实例指针（不可为 NULL）
 * @param size       FFT 点数（需为 BM_ALGO_FFT_SIZE_* 之一）
 * @param work       外部提供的工作缓冲，长度须 ≥ 2*size（不可为 NULL）
 * @param work_count work 数组元素个数
 * @return 0 成功；-1 参数非法（size 不支持或缓冲不足）
 */
int bm_algo_cfft_f32_init(bm_algo_cfft_f32_t *fft,
                          uint32_t size,
                          float *work,
                          uint32_t work_count);

/**
 * @brief 执行复数 FFT 正变换（原址）
 *
 * @param fft       已初始化的 FFT 实例（不可为 NULL）
 * @param real_imag 输入/输出复数交错数组 [re0,im0,re1,im1,...]，长度 2*size
 * @return 0 成功；-1 参数为 NULL
 */
int bm_algo_cfft_f32_forward(bm_algo_cfft_f32_t *fft, float *real_imag);

/**
 * @brief 执行复数 FFT 逆变换（原址，自动 1/N 归一化）
 *
 * @param fft       已初始化的 FFT 实例（不可为 NULL）
 * @param real_imag 输入/输出复数交错数组，长度 2*size
 * @return 0 成功；-1 参数为 NULL
 */
int bm_algo_cfft_f32_inverse(bm_algo_cfft_f32_t *fft, float *real_imag);

/**
 * @brief 初始化实数 FFT 实例
 *
 * @param fft        RFFT 实例指针（不可为 NULL）
 * @param size       FFT 点数（需为 BM_ALGO_FFT_SIZE_* 之一）
 * @param work       外部工作缓冲，长度须 ≥ 2*size（不可为 NULL）
 * @param work_count work 数组元素个数
 * @return 0 成功；-1 参数非法
 */
int bm_algo_rfft_f32_init(bm_algo_rfft_f32_t *fft,
                          uint32_t size,
                          float *work,
                          uint32_t work_count);

/**
 * @brief 对实数序列执行 FFT 并返回幅值谱（归一化到 1/N）
 *
 * @param fft         已初始化的 RFFT 实例（不可为 NULL）
 * @param time_data   时域实数输入，长度 size
 * @param spectrum_mag 输出幅值谱，长度 size/2+1（DC 到 Nyquist）
 * @return 0 成功；-1 参数为 NULL 或工作区未初始化
 */
int bm_algo_rfft_f32_execute(bm_algo_rfft_f32_t *fft,
                             const float *time_data,
                             float *spectrum_mag);

/**
 * @brief 生成 Hann 窗系数
 *
 * @param window 输出系数数组，长度 n
 * @param n      窗长度（为 0 或 NULL 时静默返回）
 */
void bm_algo_window_hann(float *window, uint32_t n);

/**
 * @brief 生成 Hamming 窗系数
 *
 * @param window 输出系数数组，长度 n
 * @param n      窗长度
 */
void bm_algo_window_hamming(float *window, uint32_t n);

/**
 * @brief 生成 Blackman 窗系数（标准三项，a0=0.42/a1=0.5/a2=0.08）
 *
 * @param window 输出系数数组，长度 n
 * @param n      窗长度
 */
void bm_algo_window_blackman(float *window, uint32_t n);

/**
 * @brief 计算窗函数的相干增益（幅值修正因子 = 窗系数均值）
 *
 * @param window 窗系数数组（不可为 NULL）
 * @param n      窗长度（为 0 返回 1.0）
 * @return 相干增益值（(0, 1]）
 */
float bm_algo_window_coherent_gain(const float *window, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FFT_H */

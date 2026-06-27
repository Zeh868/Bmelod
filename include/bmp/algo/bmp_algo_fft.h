/**
 * @file bmp_algo_fft.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief K2 · 闭源 · 需 bm_mp 的增强 FFT：Hann 窗 + RFFT + 主峰检测
 *
 * 内部封装开源 K0 `bm_algo_rfft_f32_*`。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 *
 * @par 修改日志:
 *
 * Date       Version Author Description
 * 2026-06-16 1.0     zeh    首版 MP 工业 API
 *
 */
#ifndef BMP_ALGO_FFT_H
#define BMP_ALGO_FFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMP_FFT_MAX_SIZE  64u

/** FFT 运行配置 */
typedef struct {
    uint32_t fft_size;   /**< 64/128/256…，须为 bm_algo 支持尺寸 */
    float    sample_hz;  /**< 采样率（Hz） */
} bmp_fft_config_t;

/** FFT 增强步进输出 */
typedef struct {
    uint32_t peak_bin;
    float    peak_mag;
} bmp_fft_result_t;

/** FFT 状态（调用方分配，禁止堆分配） */
typedef struct {
    uint32_t fft_size;
    float    sample_hz;
    float    work[BMP_FFT_MAX_SIZE * 2u];
    float    window[BMP_FFT_MAX_SIZE];
    float    windowed[BMP_FFT_MAX_SIZE];
    float    spectrum[BMP_FFT_MAX_SIZE / 2u + 1u];
    uint8_t  initialized;
    uint8_t  reserved[3];
} bmp_fft_state_t;

/**
 * @brief 初始化增强 FFT 状态
 *
 * @param state 状态对象，不可为 NULL
 * @param config 配置，不可为 NULL
 * @return 0 成功；负值为错误码
 */
int bmp_fft_enhanced_init(bmp_fft_state_t *state, const bmp_fft_config_t *config);

/**
 * @brief 对一块时域样本执行 Hann 窗 + RFFT 并搜索主峰
 *
 * @param state 已初始化状态，不可为 NULL
 * @param samples 时域样本，长度须 >= fft_size
 * @param sample_count 样本数
 * @param out 输出主峰信息，不可为 NULL
 * @return 0 成功；负值为错误码
 */
int bmp_fft_enhanced_step(bmp_fft_state_t *state,
                          const float *samples,
                          uint32_t sample_count,
                          bmp_fft_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_FFT_H */

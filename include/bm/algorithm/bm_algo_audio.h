/**
 * @file bm_algo_audio.h
 * @brief 音频数学核：增益、混音、均衡、限幅、AGC 与简易 VAD
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            PDM 二阶抽取
 * 2026-06-17       1.2            zeh            delay-and-sum 波束成形
 * 2026-06-17       1.3            zeh            对角加载 MVDR（E1）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_AUDIO_H
#define BM_ALGO_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bm_algo_audio_gain(const float *in, float *out, uint32_t n, float gain);
void bm_algo_audio_mix2(const float *a, const float *b, float *out,
                        uint32_t n, float gain_a, float gain_b);

typedef struct {
    float threshold;
    float knee;
} bm_algo_limiter_config_t;

void bm_algo_limiter_process(const float *in, float *out, uint32_t n,
                             const bm_algo_limiter_config_t *config);

typedef struct {
    float target_level;
    float attack_coeff;
    float release_coeff;
    float gain;              /**< Deprecated nominal gain; retained for compatibility. */
    float min_gain;          /**< <= 0 uses 0.01 */
    float max_gain;          /**< <= 0 uses 64.0 */
    float silence_threshold; /**< <= 0 uses 1e-6; gain freezes below this level */
} bm_algo_agc_config_t;

typedef struct {
    float gain;
} bm_algo_agc_state_t;

void bm_algo_agc_reset(bm_algo_agc_state_t *state, float gain_init);
void bm_algo_agc_process(bm_algo_agc_state_t *state,
                         const bm_algo_agc_config_t *config,
                         const float *in,
                         float *out,
                         uint32_t n);

typedef struct {
    float energy_threshold;
    float alpha;
} bm_algo_vad_config_t;

typedef struct {
    float energy;
    int   voice_active;
} bm_algo_vad_state_t;

void bm_algo_vad_reset(bm_algo_vad_state_t *state);
void bm_algo_vad_process(bm_algo_vad_state_t *state,
                         const bm_algo_vad_config_t *config,
                         const float *in,
                         uint32_t n);

/* ---------- Peaking EQ（Biquad 封装） ---------- */
typedef struct {
    float sample_hz;
    float freq_hz;
    float q;
    float gain_db;
} bm_algo_eq_peaking_config_t;

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float z1;
    float z2;
} bm_algo_eq_peaking_state_t;

int bm_algo_eq_peaking_design(bm_algo_eq_peaking_state_t *state,
                              const bm_algo_eq_peaking_config_t *config);
void bm_algo_eq_peaking_reset(bm_algo_eq_peaking_state_t *state);
void bm_algo_eq_peaking_process(bm_algo_eq_peaking_state_t *state,
                                const bm_algo_eq_peaking_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n);

/* ---------- 动态压缩 ---------- */
typedef struct {
    float threshold;
    float ratio;
    float attack_coeff;
    float release_coeff;
    float makeup_gain;
} bm_algo_compressor_config_t;

typedef struct {
    float envelope;
} bm_algo_compressor_state_t;

void bm_algo_compressor_reset(bm_algo_compressor_state_t *state);
void bm_algo_compressor_process(bm_algo_compressor_state_t *state,
                                const bm_algo_compressor_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n);

/* ---------- 噪声门 ---------- */
typedef struct {
    float threshold;
    float attack_coeff;
    float release_coeff;
    float floor_gain;
} bm_algo_noise_gate_config_t;

typedef struct {
    float envelope;
    float gain;
} bm_algo_noise_gate_state_t;

void bm_algo_noise_gate_reset(bm_algo_noise_gate_state_t *state);
void bm_algo_noise_gate_process(bm_algo_noise_gate_state_t *state,
                                const bm_algo_noise_gate_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n);

/* ---------- GCC-PHAT 时延估计 ---------- */
/** 正滞后：sig 相对 ref 延迟的采样数；失败返回本常量 */
#define BM_ALGO_GCC_PHAT_DELAY_INVALID  INT32_MIN

/** 工作区 float 个数：2 路复数谱，长度 4 * fft_n */
uint32_t bm_algo_gcc_phat_work_count(uint32_t n, int32_t max_lag);

int32_t bm_algo_gcc_phat_delay(const float *ref,
                               const float *sig,
                               uint32_t n,
                               int32_t max_lag,
                               float *work,
                               uint32_t work_count);

/* ---------- PDM 二阶抽取（E1） ---------- */
typedef struct {
    uint32_t decimation_factor;
    float    gain;
} bm_algo_pdm_decimate_config_t;

typedef struct {
    int32_t  integrator1;
    int32_t  integrator2;
    int32_t  comb_z1;
    int32_t  comb_z2;
    uint32_t phase;
} bm_algo_pdm_decimate_state_t;

void bm_algo_pdm_decimate_reset(bm_algo_pdm_decimate_state_t *state);

/**
 * @brief PDM 二阶 CIC 抽取：输入 ±1 样本，输出浮点 PCM
 *
 * @param state 抽取器状态
 * @param config 抽取因子与输出增益
 * @param pdm_in 每样本 -1 或 +1
 * @param pcm_out 输出缓冲
 * @param n_in 输入样本数
 * @param out_cap 输出缓冲容量
 * @return 实际输出样本数
 */
uint32_t bm_algo_pdm_decimate_block(bm_algo_pdm_decimate_state_t *state,
                                    const bm_algo_pdm_decimate_config_t *config,
                                    const int8_t *pdm_in,
                                    float *pcm_out,
                                    uint32_t n_in,
                                    uint32_t out_cap);

/**
 * @brief 多通道固定时延 delay-and-sum 波束成形
 *
 * @param channels 各通道样本指针数组（不可为 NULL）
 * @param delay_samples 各通道相对参考通道的延迟（采样数，≥0）
 * @param num_channels 通道数（≥1）
 * @param n 每通道样本数
 * @param out 单通道输出缓冲（长度 n）
 */
void bm_algo_delay_and_sum(const float * const *channels,
                           const int32_t *delay_samples,
                           uint32_t num_channels,
                           uint32_t n,
                           float *out);

/* ---------- 对角加载 MVDR（E1：非完整宽带自适应） ---------- */
typedef struct {
    float diagonal_load;   /**< 正则化，如 1e-3 */
    float sample_hz;       /**< 窄带 steering 相位参考采样率 */
} bm_algo_mvdr_config_t;

/**
 * @brief 多通道对角加载 MVDR 波束成形
 *
 * 块内估计对角协方差，固定 steering 由 delay 推导相位；2–4 通道、块内处理。
 * E1 限制：非完整自适应宽带 MVDR。
 *
 * @param channels 各通道样本指针数组（不可为 NULL）
 * @param delay_samples 各通道相对参考通道的延迟（采样数，≥0）
 * @param num_channels 通道数（2–4）
 * @param n 每通道样本数
 * @param config 对角加载与采样率（不可为 NULL）
 * @param out 单通道输出缓冲（长度 n）
 */
void bm_algo_mvdr_beamform(const float * const *channels,
                           const int32_t *delay_samples,
                           uint32_t num_channels,
                           uint32_t n,
                           const bm_algo_mvdr_config_t *config,
                           float *out);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_AUDIO_H */

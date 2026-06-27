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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_AUDIO_H
#define BM_ALGO_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 对音频块应用线性增益（逐样本乘以 gain）
 *
 * @param in   输入样本数组（不可为 NULL）
 * @param out  输出样本数组（不可为 NULL，可与 in 相同原址操作）
 * @param n    样本数
 * @param gain 线性增益值（1.0 = 无变化）
 */
void bm_algo_audio_gain(const float *in, float *out, uint32_t n, float gain);

/**
 * @brief 将两路音频按各自增益混合输出（out = a*gain_a + b*gain_b）
 *
 * @param a      输入通道 A（不可为 NULL）
 * @param b      输入通道 B（不可为 NULL）
 * @param out    输出数组（不可为 NULL）
 * @param n      样本数
 * @param gain_a 通道 A 线性增益
 * @param gain_b 通道 B 线性增益
 */
void bm_algo_audio_mix2(const float *a, const float *b, float *out,
                        uint32_t n, float gain_a, float gain_b);

/**
 * @brief 软限幅器配置
 */
typedef struct {
    float threshold; /**< 限幅门限（线性幅值，须 >0；≤0 则无限幅） */
    float knee;      /**< 软过渡区宽度（线性幅值，须 >0；≤0 则硬限幅） */
} bm_algo_limiter_config_t;

/**
 * @brief 逐样本软限幅处理（二次膝部曲线）
 *
 * 幅值 ≤ threshold：透传；threshold < 幅值 ≤ threshold+knee：二次软过渡；
 * 幅值 > threshold+knee：硬限幅到 threshold + knee/2。
 *
 * @param in     输入样本数组（不可为 NULL）
 * @param out    输出样本数组（不可为 NULL）
 * @param n      样本数
 * @param config 限幅配置（不可为 NULL）
 */
void bm_algo_limiter_process(const float *in, float *out, uint32_t n,
                             const bm_algo_limiter_config_t *config);

/**
 * @brief 自动增益控制（AGC）配置
 */
typedef struct {
    float target_level;      /**< 目标输出电平（线性幅值均值） */
    float attack_coeff;      /**< 上升（增益增大）跟踪系数，[0,1] */
    float release_coeff;     /**< 下降（增益减小）跟踪系数，[0,1] */
    float gain;              /**< 已废弃，仅保留兼容性，运行时不使用 */
    float min_gain;          /**< 增益下限（≤0 使用默认值 0.01） */
    float max_gain;          /**< 增益上限（≤0 使用默认值 64.0） */
    float silence_threshold; /**< 静音门限（≤0 使用默认值 1e-6），电平低于此值时冻结增益 */
} bm_algo_agc_config_t;

/**
 * @brief AGC 运行状态
 */
typedef struct {
    float gain; /**< 当前增益（线性值） */
} bm_algo_agc_state_t;

/**
 * @brief 重置 AGC 状态并设定初始增益
 *
 * @param state     AGC 状态（不可为 NULL）
 * @param gain_init 初始增益值（线性）
 */
void bm_algo_agc_reset(bm_algo_agc_state_t *state, float gain_init);

/**
 * @brief AGC 块处理：估算块均值电平并调整增益，输出增益后的音频
 *
 * @param state  AGC 状态（不可为 NULL）
 * @param config AGC 配置（不可为 NULL）
 * @param in     输入样本数组（不可为 NULL）
 * @param out    输出样本数组（不可为 NULL）
 * @param n      样本数（为 0 时静默返回）
 */
void bm_algo_agc_process(bm_algo_agc_state_t *state,
                         const bm_algo_agc_config_t *config,
                         const float *in,
                         float *out,
                         uint32_t n);

/**
 * @brief 简易语音活动检测（VAD）配置
 */
typedef struct {
    float energy_threshold; /**< 能量判决门限（线性均方值），超过则认为有语音 */
    float alpha;            /**< 一阶 IIR 平滑系数（[0,1]，越大跟踪越快） */
} bm_algo_vad_config_t;

/**
 * @brief VAD 运行状态
 */
typedef struct {
    float energy;      /**< 当前平滑能量估计（均方值） */
    int   voice_active;/**< 语音活动标志（1=有语音，0=静音） */
} bm_algo_vad_state_t;

/**
 * @brief 重置 VAD 状态（能量清零，voice_active=0）
 *
 * @param state VAD 状态（不可为 NULL）
 */
void bm_algo_vad_reset(bm_algo_vad_state_t *state);

/**
 * @brief VAD 块处理：计算帧均方能量，用 IIR 平滑后判决语音活动
 *
 * @param state  VAD 状态（不可为 NULL）
 * @param config VAD 配置（不可为 NULL）
 * @param in     输入样本数组（不可为 NULL）
 * @param n      样本数（为 0 时静默返回）
 */
void bm_algo_vad_process(bm_algo_vad_state_t *state,
                         const bm_algo_vad_config_t *config,
                         const float *in,
                         uint32_t n);

/* ---------- Peaking EQ（Biquad 封装） ---------- */

/**
 * @brief Peaking EQ 配置（双二阶峰值均衡滤波器参数）
 */
typedef struct {
    float sample_hz; /**< 采样率（Hz，须 >0） */
    float freq_hz;   /**< 中心频率（Hz，须 >0 且 < sample_hz/2） */
    float q;         /**< Q 值（带宽控制，须 >0） */
    float gain_db;   /**< 峰值增益（dB，正值增强，负值衰减） */
} bm_algo_eq_peaking_config_t;

/**
 * @brief Peaking EQ 运行状态（含 Biquad 系数缓存与延迟线）
 */
typedef struct {
    float b0; /**< 前向系数 b0 */
    float b1; /**< 前向系数 b1 */
    float b2; /**< 前向系数 b2 */
    float a1; /**< 反馈系数 a1 */
    float a2; /**< 反馈系数 a2 */
    float z1; /**< Biquad 内部延迟 z^{-1} */
    float z2; /**< Biquad 内部延迟 z^{-2} */
} bm_algo_eq_peaking_state_t;

/**
 * @brief 根据配置设计 Peaking EQ（计算 Biquad 系数并写入 state）
 *
 * @param state  EQ 状态（不可为 NULL，调用后需调用 reset 清延迟线）
 * @param config EQ 配置（sample_hz/freq_hz/q 须 >0）
 * @return 0 成功；-1 参数非法或 Biquad 设计失败
 */
int bm_algo_eq_peaking_design(bm_algo_eq_peaking_state_t *state,
                              const bm_algo_eq_peaking_config_t *config);

/**
 * @brief 重置 Peaking EQ 延迟线（z1/z2 清零，不改变系数）
 *
 * @param state EQ 状态（不可为 NULL）
 */
void bm_algo_eq_peaking_reset(bm_algo_eq_peaking_state_t *state);

/**
 * @brief Peaking EQ 块处理（系数为零时自动调用 design）
 *
 * @param state  EQ 状态（不可为 NULL）
 * @param config EQ 配置（不可为 NULL）
 * @param in     输入样本数组（不可为 NULL）
 * @param out    输出样本数组（不可为 NULL）
 * @param n      样本数（为 0 时静默返回）
 */
void bm_algo_eq_peaking_process(bm_algo_eq_peaking_state_t *state,
                                const bm_algo_eq_peaking_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n);

/* ---------- 动态压缩 ---------- */

/**
 * @brief 动态压缩器配置
 */
typedef struct {
    float threshold;     /**< 压缩门限（线性幅值） */
    float ratio;         /**< 压缩比（≥1.0，1.0 表示不压缩，∞ 表示限幅） */
    float attack_coeff;  /**< 包络上升跟踪系数，[0,1] */
    float release_coeff; /**< 包络下降跟踪系数，[0,1] */
    float makeup_gain;   /**< 补偿增益（线性值） */
} bm_algo_compressor_config_t;

/**
 * @brief 动态压缩器状态
 */
typedef struct {
    float envelope; /**< 当前包络估计（线性幅值） */
} bm_algo_compressor_state_t;

/**
 * @brief 重置动态压缩器状态（包络清零）
 *
 * @param state 压缩器状态（不可为 NULL）
 */
void bm_algo_compressor_reset(bm_algo_compressor_state_t *state);

/**
 * @brief 动态压缩器块处理（逐样本包络检测 + 可变增益 + makeup）
 *
 * @param state  压缩器状态（不可为 NULL）
 * @param config 压缩器配置（不可为 NULL）
 * @param in     输入样本数组（不可为 NULL）
 * @param out    输出样本数组（不可为 NULL）
 * @param n      样本数（为 0 时静默返回）
 */
void bm_algo_compressor_process(bm_algo_compressor_state_t *state,
                                const bm_algo_compressor_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n);

/* ---------- 噪声门 ---------- */

/**
 * @brief 噪声门配置
 */
typedef struct {
    float threshold;     /**< 开门门限（线性幅值），超过则开门 */
    float attack_coeff;  /**< 开门（增益上升）跟踪系数，[0,1] */
    float release_coeff; /**< 关门（增益下降）跟踪系数，[0,1] */
    float floor_gain;    /**< 关门时的残余增益（[0,1]，0 表示完全静音） */
} bm_algo_noise_gate_config_t;

/**
 * @brief 噪声门状态
 */
typedef struct {
    float envelope; /**< 当前包络估计（线性幅值） */
    float gain;     /**< 当前门增益（[floor_gain, 1.0]） */
} bm_algo_noise_gate_state_t;

/**
 * @brief 重置噪声门状态（包络清零，增益置 1.0）
 *
 * @param state 噪声门状态（不可为 NULL）
 */
void bm_algo_noise_gate_reset(bm_algo_noise_gate_state_t *state);

/**
 * @brief 噪声门块处理（逐样本包络检测 + 平滑增益控制）
 *
 * @param state  噪声门状态（不可为 NULL）
 * @param config 噪声门配置（不可为 NULL）
 * @param in     输入样本数组（不可为 NULL）
 * @param out    输出样本数组（不可为 NULL）
 * @param n      样本数（为 0 时静默返回）
 */
void bm_algo_noise_gate_process(bm_algo_noise_gate_state_t *state,
                                const bm_algo_noise_gate_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n);

/* ---------- GCC-PHAT 时延估计 ---------- */

/** 正滞后表示 sig 相对 ref 延迟的采样数；函数失败时返回本常量（INT32_MIN） */
#define BM_ALGO_GCC_PHAT_DELAY_INVALID  INT32_MIN

/**
 * @brief 查询 GCC-PHAT 所需工作缓冲大小
 *
 * 工作区包含 2 路复数谱，每路长度 2*fft_n，合计 4*fft_n 个 float。
 *
 * @param n       每路信号长度（采样数）
 * @param max_lag 最大搜索滞后（采样数，须 ≥0）
 * @return 所需 float 元素个数；参数非法或超出支持范围时返回 0
 */
uint32_t bm_algo_gcc_phat_work_count(uint32_t n, int32_t max_lag);

/**
 * @brief 基于 GCC-PHAT 估计两路信号的到达时延差（TDOA）
 *
 * 算法流程：零填充 → 两路 FFT → PHAT 归一化互功率谱 → IFFT → 搜索最大值。
 * 在 [-max_lag, max_lag] 范围内搜索绝对值最大的滞后作为 TDOA 估计。
 *
 * @param ref        参考信号（不可为 NULL），长度 n
 * @param sig        待测信号（不可为 NULL），长度 n
 * @param n          每路信号长度（须 ≤ BM_ALGO_FFT_SIZE_1024）
 * @param max_lag    最大搜索滞后范围（须 ≥0）
 * @param work       工作缓冲（不可为 NULL），长度须 ≥ bm_algo_gcc_phat_work_count()
 * @param work_count work 数组元素个数
 * @return 估计的 TDOA（正值表示 sig 落后 ref 的采样数）；失败返回 BM_ALGO_GCC_PHAT_DELAY_INVALID
 */
int32_t bm_algo_gcc_phat_delay(const float *ref,
                               const float *sig,
                               uint32_t n,
                               int32_t max_lag,
                               float *work,
                               uint32_t work_count);

/* ---------- PDM 二阶抽取（E1） ---------- */

/**
 * @brief PDM CIC 二阶抽取配置
 */
typedef struct {
    uint32_t decimation_factor; /**< 抽取因子 R（须 ≥1，典型值 32/64/128） */
    float    gain;              /**< 输出增益（≤0 使用默认值 1.0） */
} bm_algo_pdm_decimate_config_t;

/**
 * @brief PDM CIC 二阶抽取器内部状态
 *
 * 结构对应 2 级积分器 + 2 级梳状滤波器的 CIC 拓扑：
 * integrator1/2 为累加寄存器，comb_z1/z2 为梳状级延迟。
 */
typedef struct {
    int32_t  integrator1; /**< 第一级积分器累加值 */
    int32_t  integrator2; /**< 第二级积分器累加值 */
    int32_t  comb_z1;     /**< 第一级梳状滤波器延迟寄存器 */
    int32_t  comb_z2;     /**< 第二级梳状滤波器延迟寄存器 */
    uint32_t phase;       /**< 抽取相位计数器（[0, R)） */
} bm_algo_pdm_decimate_state_t;

/**
 * @brief 重置 PDM CIC 抽取器状态（所有寄存器清零）
 *
 * @param state 抽取器状态（不可为 NULL）
 */
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

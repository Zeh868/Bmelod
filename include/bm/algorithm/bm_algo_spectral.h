/**
 * @file bm_algo_spectral.h
 * @brief 频谱分析：Goertzel、PSD、包络、相关与谱峰
 *
 * @maturity E1
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
 * 2026-06-23       1.3            zeh            bm_algo_stft_overlap_init 注释标注 frame_size<=64 上限约束
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_SPECTRAL_H
#define BM_ALGO_SPECTRAL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Goertzel 单频检测 ---------- */
typedef struct {
    float target_freq_hz;
    float sample_hz;
    uint32_t block_size;
    float coeff;
} bm_algo_goertzel_config_t;

typedef struct {
    float s_prev;
    float s_prev2;
    float coeff;
    uint32_t count;
} bm_algo_goertzel_state_t;

int bm_algo_goertzel_init(bm_algo_goertzel_state_t *state,
                          const bm_algo_goertzel_config_t *config);
void bm_algo_goertzel_reset(bm_algo_goertzel_state_t *state);
int bm_algo_goertzel_feed(bm_algo_goertzel_state_t *state,
                          const bm_algo_goertzel_config_t *config,
                          float sample);
float bm_algo_goertzel_result(bm_algo_goertzel_state_t *state,
                              const bm_algo_goertzel_config_t *config);

/* ---------- PSD（周期图，输入为幅度谱平方） ---------- */
void bm_algo_psd_from_spectrum(const float *mag,
                               uint32_t bin_count,
                               float scale,
                               float *psd);

/* ---------- Hilbert 包络（FIR 近似 + 解析信号幅值） ---------- */
typedef struct {
    float prev;
    float envelope;
    float alpha;
} bm_algo_envelope_state_t;

void bm_algo_envelope_reset(bm_algo_envelope_state_t *state);
float bm_algo_envelope_step(bm_algo_envelope_state_t *state, float input);

/* ---------- 互相关（有限长度） ---------- */
float bm_algo_correlate(const float *a,
                        const float *b,
                        uint32_t len);

/* ---------- 谱峰搜索 ---------- */
int bm_algo_find_peak_bin(const float *spectrum,
                          uint32_t start_bin,
                          uint32_t end_bin,
                          uint32_t *peak_bin,
                          float *peak_value);

/* ---------- STFT 幅度谱（单帧） ---------- */
int bm_algo_stft_magnitude_frame(const float *frame,
                                 const float *window,
                                 uint32_t n,
                                 float *magnitude);

/* ---------- 重叠 STFT 流式状态机 ---------- */
typedef struct {
    uint32_t frame_size;
    uint32_t hop_size;
    const float *window;
} bm_algo_stft_overlap_config_t;

typedef struct {
    uint32_t frame_size;
    uint32_t hop_size;
    float *ring_buffer;
    uint32_t write_idx;
    uint32_t filled;
    uint32_t samples_since_hop;
    uint32_t frame_count;
} bm_algo_stft_overlap_t;

/**
 * @brief 初始化重叠 STFT 状态机
 *
 * @param state         状态对象（不可为 NULL）
 * @param config        配置（不可为 NULL）；frame_size 须满足 2 <= frame_size <= 64，
 *                      超出上限时返回 -1（与 feed 内部 64 点栈帧限制一致）
 * @param ring_buffer   调用方提供的环形缓冲，长度 >= frame_size
 * @param ring_buffer_len  ring_buffer 可用长度
 * @return 0 成功；-1 参数无效或 frame_size 超限
 */
int bm_algo_stft_overlap_init(bm_algo_stft_overlap_t *state,
                              const bm_algo_stft_overlap_config_t *config,
                              float *ring_buffer,
                              uint32_t ring_buffer_len);
void bm_algo_stft_overlap_reset(bm_algo_stft_overlap_t *state);
/**
 * @brief 喂入单样本；hop 到达时计算一帧幅度谱
 *
 * @param magnitude_out 调用者工作区，长度 >= frame_size/2 + 1
 * @param magnitude_len 缓冲区长度
 * @return 1 输出一帧；0 继续积累；-1 参数错误
 */
int bm_algo_stft_overlap_feed(bm_algo_stft_overlap_t *state,
                              const bm_algo_stft_overlap_config_t *config,
                              float sample,
                              float *magnitude_out,
                              uint32_t magnitude_len);

/* ---------- 阶次换算 ---------- */
float bm_algo_order_from_hz(float freq_hz, float rpm, float pole_pairs_or_harmonic);

/* ---------- 阶次跟踪（E1） ---------- */
typedef struct {
    float sample_hz;
    float pole_pairs;
    float lpf_alpha;
} bm_algo_order_tracker_config_t;

typedef struct {
    float filtered_order;
    float shaft_hz;
} bm_algo_order_tracker_state_t;

void bm_algo_order_tracker_reset(bm_algo_order_tracker_state_t *state);

/**
 * @brief 喂入转速提示或谱峰频率，输出滤波阶次与轴频
 *
 * @param rpm_hint 机械转速（rpm）；peak_freq_hz>0 时用于轴频
 * @param peak_freq_hz 谱峰频率（Hz）；为 0 时仅用 rpm_hint
 */
void bm_algo_order_tracker_feed(bm_algo_order_tracker_state_t *state,
                                const bm_algo_order_tracker_config_t *config,
                                float rpm_hint,
                                float peak_freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_SPECTRAL_H */

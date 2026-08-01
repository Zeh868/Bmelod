/**
 * @file bm_algo_spectral.h
 * @brief 频谱分析：Goertzel、PSD、包络、相关与谱峰
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 STFT 幅度谱与阶次换算
 * 2026-06-17       1.2            zeh            增加重叠 STFT 状态机
 * 2026-06-23       1.3            zeh            bm_algo_stft_overlap_init 注释标注 frame_size<=64 上限约束
 * 2026-07-28       1.4            zeh            明确帧就绪返回值载荷及 BM_ERR_* 错误
 * 2026-08-01       1.4            zeh          补齐公共 API 中文 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_SPECTRAL_H
#define BM_ALGO_SPECTRAL_H

#include "bm/algorithm/bm_algo_errors.h"

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

/**
 * @brief 初始化 Goertzel 单频分析器。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @return 成功返回 BM_OK；参数、配置或缓冲区无效时返回 BM_ERR_INVALID。
 */
int bm_algo_goertzel_init(bm_algo_goertzel_state_t *state,
                          const bm_algo_goertzel_config_t *config);
/**
 * @brief 复位 Goertzel 单频分析器状态。
 * @param state 算法状态对象。
 */
void bm_algo_goertzel_reset(bm_algo_goertzel_state_t *state);
/**
 * @brief 向 Goertzel 单频分析器输入一个样本。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param sample 当前输入样本。
 * @return 累计满一个分析块时返回 1，尚未满时返回 0；参数无效时返回 BM_ERR_INVALID。
 */
int bm_algo_goertzel_feed(bm_algo_goertzel_state_t *state,
                          const bm_algo_goertzel_config_t *config,
                          float sample);
/**
 * @brief 计算并返回 Goertzel 单频分析器结果。
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @return 返回目标频点的幅值；参数无效时返回 0。
 */
float bm_algo_goertzel_result(bm_algo_goertzel_state_t *state,
                              const bm_algo_goertzel_config_t *config);

/* ---------- PSD（周期图，输入为幅度谱平方） ---------- */
/**
 * @brief 将幅值谱换算为功率谱密度。
 * @param mag 输入的幅值谱数组。
 * @param bin_count 幅值谱的 bin 数量。
 * @param scale 功率谱密度换算比例。
 * @param psd 输出的功率谱密度数组。
 */
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

/**
 * @brief 复位信号包络跟踪器状态。
 * @param state 算法状态对象。
 */
void bm_algo_envelope_reset(bm_algo_envelope_state_t *state);
/**
 * @brief 执行一次信号包络跟踪器更新。
 * @param state 算法状态对象。
 * @param input 当前输入样本。
 * @return 当前包络幅值；state 为 NULL 时直通返回 input。
 */
float bm_algo_envelope_step(bm_algo_envelope_state_t *state, float input);

/* ---------- 互相关（有限长度） ---------- */
/**
 * @brief 计算两个等长序列的点积相关值。
 * @param a 第一路输入值或序列。
 * @param b 第二路输入值或序列。
 * @param len 输入序列长度。
 * @return 返回两个序列的点积和；输入数组无效时返回 0。
 */
float bm_algo_correlate(const float *a,
                        const float *b,
                        uint32_t len);

/* ---------- 谱峰搜索 ---------- */
/**
 * @brief 在指定频谱区间内查找最大峰值及其 bin。
 * @param spectrum 输入的频谱数组。
 * @param start_bin 搜索区间的起始 bin。
 * @param end_bin 搜索区间的结束 bin。
 * @param peak_bin 输出的峰值 bin 索引。
 * @param peak_value 输出的峰值幅值。
 * @return 成功返回 BM_OK；参数、配置或缓冲区无效时返回 BM_ERR_INVALID。
 */
int bm_algo_find_peak_bin(const float *spectrum,
                          uint32_t start_bin,
                          uint32_t end_bin,
                          uint32_t *peak_bin,
                          float *peak_value);

/* ---------- STFT 幅度谱（单帧） ---------- */
/**
 * @brief 对加窗后的单帧数据计算 STFT 幅值谱。
 * @param frame 输入的时域帧。
 * @param window 与输入帧等长的窗函数系数。
 * @param n 输入帧和窗函数的样本数。
 * @param magnitude 输出的幅值谱数组。
 * @return 成功返回 BM_OK；参数、配置或缓冲区无效时返回 BM_ERR_INVALID。
 */
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
 *                      超出上限时返回 BM_ERR_INVALID（与 feed 内部 64 点栈帧限制一致）
 * @param ring_buffer   调用方提供的环形缓冲，长度 >= frame_size
 * @param ring_buffer_len  ring_buffer 可用长度
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或 frame_size 超限
 */
int bm_algo_stft_overlap_init(bm_algo_stft_overlap_t *state,
                              const bm_algo_stft_overlap_config_t *config,
                              float *ring_buffer,
                              uint32_t ring_buffer_len);
/**
 * @brief 复位重叠 STFT 分析器状态。
 * @param state 算法状态对象。
 */
void bm_algo_stft_overlap_reset(bm_algo_stft_overlap_t *state);
/**
 * @brief 喂入单样本；hop 到达时计算一帧幅度谱
 *
 * @param magnitude_out 调用者工作区，长度 >= frame_size/2 + 1
 * @param magnitude_len 缓冲区长度
 * @return 1 输出一帧；0 继续积累（返回值即载荷）；BM_ERR_INVALID 参数错误；
 *         内部栈帧缓冲容量不足时返回 BM_ERR_OVERFLOW
 *
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param sample 当前输入样本。
 */
int bm_algo_stft_overlap_feed(bm_algo_stft_overlap_t *state,
                              const bm_algo_stft_overlap_config_t *config,
                              float sample,
                              float *magnitude_out,
                              uint32_t magnitude_len);

/* ---------- 阶次换算 ---------- */
/**
 * @brief 根据转速和极对数或谐波倍数换算阶次。
 * @param freq_hz 待换算频率，单位 Hz。
 * @param rpm 机械转速，单位 r/min。
 * @param pole_pairs_or_harmonic 电机极对数或目标谐波倍数。
 * @return 返回换算后的阶次；输入无效时返回 0。
 */
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

/**
 * @brief 复位阶次跟踪器状态。
 * @param state 算法状态对象。
 */
void bm_algo_order_tracker_reset(bm_algo_order_tracker_state_t *state);

/**
 * @brief 喂入转速提示或谱峰频率，输出滤波阶次与轴频
 *
 * @param rpm_hint 机械转速（rpm）；peak_freq_hz>0 时用于轴频
 * @param peak_freq_hz 谱峰频率（Hz）；为 0 时仅用 rpm_hint
 *
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 */
void bm_algo_order_tracker_feed(bm_algo_order_tracker_state_t *state,
                                const bm_algo_order_tracker_config_t *config,
                                float rpm_hint,
                                float peak_freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_SPECTRAL_H */

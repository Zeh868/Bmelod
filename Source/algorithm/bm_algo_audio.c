/**
 * @file bm_algo_audio.c
 * @brief 音频数学核实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 EQ/compressor/gate/GCC-PHAT
 * 2026-06-17       1.2            zeh            PDM 二阶 CIC 抽取
 * 2026-06-17       1.3            zeh            delay-and-sum 波束成形
 * 2026-06-17       1.4            zeh            对角加载 MVDR
 * 2026-06-23       1.4            zeh            补齐 Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_audio.h"
#include "bm/algorithm/bm_algo_errors.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_fft.h"
#include <stddef.h>

#include <math.h>
#include <string.h>

void bm_algo_audio_gain(const float *in, float *out, uint32_t n, float gain) {
    uint32_t i;

    if (in == NULL || out == NULL) {
        return;
    }
    for (i = 0u; i < n; ++i) {
        out[i] = in[i] * gain;
    }
}

void bm_algo_audio_mix2(const float *a, const float *b, float *out,
                        uint32_t n, float gain_a, float gain_b) {
    uint32_t i;

    if (a == NULL || b == NULL || out == NULL) {
        return;
    }
    for (i = 0u; i < n; ++i) {
        out[i] = a[i] * gain_a + b[i] * gain_b;
    }
}

void bm_algo_limiter_process(const float *in, float *out, uint32_t n,
                             const bm_algo_limiter_config_t *config) {
    uint32_t i;
    float th;
    float knee;

    if (in == NULL || out == NULL || config == NULL) {
        return;
    }

    th = (config->threshold > 0.0f) ? config->threshold : 0.0f;
    knee = (config->knee > 0.0f) ? config->knee : 0.0f;

    for (i = 0u; i < n; ++i) {
        float x = in[i];
        float ax = fabsf(x);

        if (ax <= th) {
            out[i] = x;
        } else if (ax <= th + knee) {
            float t = (ax - th) / knee;
            float gain = th + knee * t * t * 0.5f;
            out[i] = (x > 0.0f ? 1.0f : -1.0f) * gain;
        } else {
            out[i] = (x > 0.0f ? 1.0f : -1.0f) * (th + knee * 0.5f);
        }
    }
}

void bm_algo_agc_reset(bm_algo_agc_state_t *state, float gain_init) {
    if (state != NULL) {
        state->gain = gain_init;
    }
}

void bm_algo_agc_process(bm_algo_agc_state_t *state,
                         const bm_algo_agc_config_t *config,
                         const float *in,
                         float *out,
                         uint32_t n) {
    uint32_t i;
    float level;
    float err;
    float coeff;
    float min_gain;
    float max_gain;
    float silence_threshold;

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        n == 0u) {
        return;
    }

    level = 0.0f;
    for (i = 0u; i < n; ++i) {
        level += fabsf(in[i]);
    }
    level /= (float)n;

    min_gain = (config->min_gain > 0.0f) ? config->min_gain : 0.01f;
    max_gain = (config->max_gain >= min_gain) ? config->max_gain : 64.0f;
    silence_threshold = (config->silence_threshold > 0.0f)
                            ? config->silence_threshold
                            : 1e-6f;

    if (!bm_algo_is_finite_f(state->gain)) {
        state->gain = min_gain;
    }
    if (level >= silence_threshold && bm_algo_is_finite_f(level)) {
        err = config->target_level - level * state->gain;
        coeff = (err > 0.0f) ? config->attack_coeff : config->release_coeff;
        coeff = bm_algo_clamp_f(coeff, 0.0f, 1.0f);
        state->gain += coeff * err;
    }
    state->gain = bm_algo_clamp_f(state->gain, min_gain, max_gain);

    for (i = 0u; i < n; ++i) {
        out[i] = in[i] * state->gain;
    }
}

void bm_algo_vad_reset(bm_algo_vad_state_t *state) {
    if (state != NULL) {
        state->energy = 0.0f;
        state->voice_active = 0;
    }
}

void bm_algo_vad_process(bm_algo_vad_state_t *state,
                         const bm_algo_vad_config_t *config,
                         const float *in,
                         uint32_t n) {
    uint32_t i;
    float e = 0.0f;

    if (state == NULL || config == NULL || in == NULL || n == 0u) {
        return;
    }

    for (i = 0u; i < n; ++i) {
        e += in[i] * in[i];
    }
    e /= (float)n;

    state->energy += config->alpha * (e - state->energy);
    state->voice_active = (state->energy > config->energy_threshold) ? 1 : 0;
}

int bm_algo_eq_peaking_design(bm_algo_eq_peaking_state_t *state,
                              const bm_algo_eq_peaking_config_t *config) {
    bm_algo_biquad_config_t bq;
    bm_algo_biquad_design_t design;

    if (state == NULL || config == NULL ||
        config->sample_hz <= 0.0f || config->freq_hz <= 0.0f ||
        config->q <= 0.0f) {
        return BM_ALGO_ERR_INVALID;
    }

    design.type = BM_ALGO_BIQUAD_PEAKING;
    design.sample_hz = config->sample_hz;
    design.freq_hz = config->freq_hz;
    design.q = config->q;
    design.gain_db = config->gain_db;

    if (bm_algo_biquad_design(&bq, &design) != 0) {
        return BM_ALGO_ERR_INVALID;
    }

    state->b0 = bq.b0;
    state->b1 = bq.b1;
    state->b2 = bq.b2;
    state->a1 = bq.a1;
    state->a2 = bq.a2;
    bm_algo_eq_peaking_reset(state);
    return 0;
}

void bm_algo_eq_peaking_reset(bm_algo_eq_peaking_state_t *state) {
    if (state != NULL) {
        state->z1 = 0.0f;
        state->z2 = 0.0f;
    }
}

void bm_algo_eq_peaking_process(bm_algo_eq_peaking_state_t *state,
                                const bm_algo_eq_peaking_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n) {
    uint32_t i;
    bm_algo_biquad_state_t bq_state;
    bm_algo_biquad_config_t bq_config;

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        n == 0u) {
        return;
    }

    if (state->b0 == 0.0f && state->b1 == 0.0f && state->b2 == 0.0f) {
        if (bm_algo_eq_peaking_design(state, config) != 0) {
            for (i = 0u; i < n; ++i) {
                out[i] = in[i];
            }
            return;
        }
    }

    bq_state.z1 = state->z1;
    bq_state.z2 = state->z2;
    bq_config.b0 = state->b0;
    bq_config.b1 = state->b1;
    bq_config.b2 = state->b2;
    bq_config.a1 = state->a1;
    bq_config.a2 = state->a2;

    for (i = 0u; i < n; ++i) {
        out[i] = bm_algo_biquad_step(&bq_state, &bq_config, in[i]);
    }

    state->z1 = bq_state.z1;
    state->z2 = bq_state.z2;
}

void bm_algo_compressor_reset(bm_algo_compressor_state_t *state) {
    if (state != NULL) {
        state->envelope = 0.0f;
    }
}

void bm_algo_compressor_process(bm_algo_compressor_state_t *state,
                                const bm_algo_compressor_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n) {
    uint32_t i;
    float th;
    float ratio;
    float atk;
    float rel;

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        n == 0u) {
        return;
    }

    th = config->threshold;
    ratio = (config->ratio > 1.0f) ? config->ratio : 1.0f;
    atk = bm_algo_clamp_f(config->attack_coeff, 0.0f, 1.0f);
    rel = bm_algo_clamp_f(config->release_coeff, 0.0f, 1.0f);

    for (i = 0u; i < n; ++i) {
        float x = in[i];
        float ax = fabsf(x);
        float gain;
        float coeff;

        coeff = (ax > state->envelope) ? atk : rel;
        state->envelope += coeff * (ax - state->envelope);

        if (state->envelope <= th) {
            gain = config->makeup_gain;
        } else {
            float over = state->envelope - th;
            float compressed = th + over / ratio;
            gain = (state->envelope > 1e-12f)
                       ? (compressed / state->envelope) * config->makeup_gain
                       : config->makeup_gain;
        }

        out[i] = x * gain;
    }
}

void bm_algo_noise_gate_reset(bm_algo_noise_gate_state_t *state) {
    if (state != NULL) {
        state->envelope = 0.0f;
        state->gain = 1.0f;
    }
}

void bm_algo_noise_gate_process(bm_algo_noise_gate_state_t *state,
                                const bm_algo_noise_gate_config_t *config,
                                const float *in,
                                float *out,
                                uint32_t n) {
    uint32_t i;
    float th;
    float atk;
    float rel;
    float floor;

    if (state == NULL || config == NULL || in == NULL || out == NULL ||
        n == 0u) {
        return;
    }

    th = config->threshold;
    atk = bm_algo_clamp_f(config->attack_coeff, 0.0f, 1.0f);
    rel = bm_algo_clamp_f(config->release_coeff, 0.0f, 1.0f);
    floor = bm_algo_clamp_f(config->floor_gain, 0.0f, 1.0f);

    for (i = 0u; i < n; ++i) {
        float x = in[i];
        float ax = fabsf(x);
        float target;
        float coeff;

        coeff = (ax > state->envelope) ? atk : rel;
        state->envelope += coeff * (ax - state->envelope);

        target = (state->envelope >= th) ? 1.0f : floor;
        coeff = (target > state->gain) ? atk : rel;
        state->gain += coeff * (target - state->gain);
        out[i] = x * state->gain;
    }
}

#define GCC_PHAT_FFT_MAX BM_ALGO_FFT_SIZE_1024

/**
 * @brief 根据线性相关长度选取最小满足的 FFT 点数
 *
 * 从 64/128/256/512/1024 中选出第一个 ≥ linear_n 的值。
 * 若 linear_n 超过最大支持点数，返回 0 表示不支持。
 *
 * @param linear_n 所需最小 FFT 点数
 * @return 实际 FFT 点数（BM_ALGO_FFT_SIZE_*），或 0 表示超出范围
 */
static uint32_t gcc_phat_pick_fft_size(uint32_t linear_n) {
    if (linear_n <= BM_ALGO_FFT_SIZE_64) {
        return BM_ALGO_FFT_SIZE_64;
    }
    if (linear_n <= BM_ALGO_FFT_SIZE_128) {
        return BM_ALGO_FFT_SIZE_128;
    }
    if (linear_n <= BM_ALGO_FFT_SIZE_256) {
        return BM_ALGO_FFT_SIZE_256;
    }
    if (linear_n <= BM_ALGO_FFT_SIZE_512) {
        return BM_ALGO_FFT_SIZE_512;
    }
    if (linear_n <= BM_ALGO_FFT_SIZE_1024) {
        return BM_ALGO_FFT_SIZE_1024;
    }
    return 0u;
}

/**
 * @brief 计算 GCC-PHAT 所需的线性相关序列长度
 *
 * 线性相关长度 = n + max_lag，用于确定 FFT 点数以避免圆形混叠。
 * 若加法溢出 UINT32_MAX 则返回 0 表示不可计算。
 *
 * @param n       信号长度（采样数）
 * @param max_lag 最大搜索滞后（采样数，须 ≥0）
 * @return 线性相关序列长度；溢出时返回 0
 */
static uint32_t gcc_phat_linear_len(uint32_t n, int32_t max_lag) {
    uint32_t extra = (max_lag > 0) ? (uint32_t)max_lag : 0u;

    if (extra > UINT32_MAX - n) {
        return 0u;
    }
    return n + extra;
}

uint32_t bm_algo_gcc_phat_work_count(uint32_t n, int32_t max_lag) {
    uint32_t linear_n;
    uint32_t fft_n;

    if (n == 0u || max_lag < 0 || n > GCC_PHAT_FFT_MAX) {
        return 0u;
    }
    linear_n = gcc_phat_linear_len(n, max_lag);
    if (linear_n == 0u) {
        return 0u;
    }
    fft_n = gcc_phat_pick_fft_size(linear_n);
    if (fft_n == 0u) {
        return 0u;
    }
    return 4u * fft_n;
}

/**
 * @brief 将实数序列填充为零填充复数交错数组
 *
 * 前 n 个复数点的实部取自 src[]，虚部补零；超出 n 的点全零填充。
 * 此步骤实现 FFT 零填充以抑制圆形卷积混叠。
 *
 * @param ri    输出复数交错缓冲 [re,im,...]，长度 2*fft_n
 * @param fft_n FFT 点数
 * @param src   输入实数数组，长度 n
 * @param n     有效输入样本数（n ≤ fft_n）
 */
static void gcc_phat_fill_time(float *ri, uint32_t fft_n, const float *src,
                               uint32_t n) {
    uint32_t i;

    for (i = 0u; i < fft_n; ++i) {
        if (i < n) {
            ri[2u * i] = src[i];
        } else {
            ri[2u * i] = 0.0f;
        }
        ri[2u * i + 1u] = 0.0f;
    }
}

/**
 * @brief 原址计算归一化互功率谱（PHAT 加权）
 *
 * PHAT 加权公式：R(k) = X_ref(k) * conj(X_sig(k)) / |X_ref(k) * conj(X_sig(k))|
 * 归一化使所有频率分量幅值为 1，增强时延估计对宽带噪声的鲁棒性。
 * 幅值低于 1e-12 时将该频率箱置零以防止除零。
 *
 * @param ref_spec 参考信号频谱（原址修改为归一化互功率谱），长度 2*fft_n
 * @param sig_spec 信号频谱（只读），长度 2*fft_n
 * @param fft_n    FFT 点数
 */
static void gcc_phat_apply_phat(float *ref_spec, const float *sig_spec,
                                uint32_t fft_n) {
    uint32_t i;

    for (i = 0u; i < fft_n; ++i) {
        float rr = ref_spec[2u * i];
        float ri = ref_spec[2u * i + 1u];
        float sr = sig_spec[2u * i];
        float si = sig_spec[2u * i + 1u];
        float cr = rr * sr + ri * si;
        float ci = -ri * sr + rr * si;
        float mag = sqrtf(cr * cr + ci * ci);

        if (mag > 1e-12f) {
            ref_spec[2u * i] = cr / mag;
            ref_spec[2u * i + 1u] = ci / mag;
        } else {
            ref_spec[2u * i] = 0.0f;
            ref_spec[2u * i + 1u] = 0.0f;
        }
    }
}

/**
 * @brief 从 IFFT 结果中读取指定滞后处的 GCC-PHAT 相关值
 *
 * 正滞后 lag ≥ 0 对应线性索引 lag；
 * 负滞后 lag < 0 利用循环对称，对应索引 fft_n - |lag|。
 * 返回该复数点的实部（IFFT 后相关函数实部即为 GCC 值）。
 *
 * @param gcc   IFFT 结果（复数交错），长度 2*fft_n
 * @param fft_n FFT 点数
 * @param lag   查询滞后（采样数，[-max_lag, max_lag]）
 * @return 该滞后处的 GCC-PHAT 相关值（实部）；越界返回 0.0
 */
static float gcc_phat_corr_at_lag(const float *gcc, uint32_t fft_n,
                                  int32_t lag) {
    uint32_t idx;

    if (lag >= 0) {
        idx = (uint32_t)lag;
    } else {
        idx = fft_n - (uint32_t)(-lag);
    }

    if (idx >= fft_n) {
        return 0.0f;
    }
    return gcc[2u * idx];
}

int32_t bm_algo_gcc_phat_delay(const float *ref,
                               const float *sig,
                               uint32_t n,
                               int32_t max_lag,
                               float *work,
                               uint32_t work_count) {
    float *work_r;
    float *work_s;
    bm_algo_cfft_f32_t fft;
    uint32_t fft_n;
    uint32_t linear_n;
    int32_t lag;
    int32_t best_lag = 0;
    float best = -1.0f;
    float c;

    if (ref == NULL || sig == NULL || work == NULL || n == 0u || max_lag < 0 ||
        n > GCC_PHAT_FFT_MAX) {
        return BM_ALGO_GCC_PHAT_DELAY_INVALID;
    }

    linear_n = gcc_phat_linear_len(n, max_lag);
    if (linear_n == 0u) {
        return BM_ALGO_GCC_PHAT_DELAY_INVALID;
    }
    fft_n = gcc_phat_pick_fft_size(linear_n);
    if (fft_n == 0u || work_count < 4u * fft_n) {
        return BM_ALGO_GCC_PHAT_DELAY_INVALID;
    }

    work_r = work;
    work_s = work + (2u * fft_n);

    gcc_phat_fill_time(work_r, fft_n, ref, n);
    gcc_phat_fill_time(work_s, fft_n, sig, n);

    if (bm_algo_cfft_f32_init(&fft, fft_n, work_r, 2u * fft_n) != 0 ||
        bm_algo_cfft_f32_forward(&fft, work_r) != 0) {
        return BM_ALGO_GCC_PHAT_DELAY_INVALID;
    }
    if (bm_algo_cfft_f32_init(&fft, fft_n, work_s, 2u * fft_n) != 0 ||
        bm_algo_cfft_f32_forward(&fft, work_s) != 0) {
        return BM_ALGO_GCC_PHAT_DELAY_INVALID;
    }

    gcc_phat_apply_phat(work_r, work_s, fft_n);

    if (bm_algo_cfft_f32_init(&fft, fft_n, work_r, 2u * fft_n) != 0 ||
        bm_algo_cfft_f32_inverse(&fft, work_r) != 0) {
        return BM_ALGO_GCC_PHAT_DELAY_INVALID;
    }

    for (lag = -max_lag; lag <= max_lag; ++lag) {
        c = fabsf(gcc_phat_corr_at_lag(work_r, fft_n, lag));
        if (c > best) {
            best = c;
            best_lag = lag;
        }
    }

    return best_lag;
}

void bm_algo_pdm_decimate_reset(bm_algo_pdm_decimate_state_t *state) {
    if (state != NULL) {
        state->integrator1 = 0;
        state->integrator2 = 0;
        state->comb_z1 = 0;
        state->comb_z2 = 0;
        state->phase = 0u;
    }
}

uint32_t bm_algo_pdm_decimate_block(bm_algo_pdm_decimate_state_t *state,
                                    const bm_algo_pdm_decimate_config_t *config,
                                    const int8_t *pdm_in,
                                    float *pcm_out,
                                    uint32_t n_in,
                                    uint32_t out_cap) {
    uint32_t i;
    uint32_t out_count = 0u;
    uint32_t R;
    float gain;

    if (state == NULL || config == NULL || pdm_in == NULL ||
        pcm_out == NULL || n_in == 0u || out_cap == 0u ||
        config->decimation_factor == 0u) {
        return 0u;
    }

    R = config->decimation_factor;
    gain = (config->gain > 0.0f) ? config->gain : 1.0f;

    for (i = 0u; i < n_in; ++i) {
        int32_t bit = (pdm_in[i] > 0) ? 1 : -1;
        int32_t diff2;
        int32_t diff1;

        state->integrator1 += bit;
        state->integrator2 += state->integrator1;

        state->phase++;
        if (state->phase < R) {
            continue;
        }
        state->phase = 0u;

        diff1 = state->integrator2 - state->comb_z1;
        state->comb_z1 = state->integrator2;
        diff2 = diff1 - state->comb_z2;
        state->comb_z2 = diff1;

        if (out_count < out_cap) {
            pcm_out[out_count] = (float)diff2 * gain /
                                 (float)((int64_t)R * (int64_t)R);
            out_count++;
        }
    }

    return out_count;
}

void bm_algo_delay_and_sum(const float * const *channels,
                           const int32_t *delay_samples,
                           uint32_t num_channels,
                           uint32_t n,
                           float *out) {
    uint32_t i;
    uint32_t ch;

    if (channels == NULL || delay_samples == NULL || out == NULL ||
        num_channels == 0u || n == 0u) {
        return;
    }

    for (i = 0u; i < n; ++i) {
        float sum = 0.0f;

        for (ch = 0u; ch < num_channels; ++ch) {
            int32_t delay = delay_samples[ch];
            uint32_t src_idx;

            if (channels[ch] == NULL) {
                continue;
            }
            if (delay < 0) {
                delay = 0;
            }
            if ((uint32_t)delay > i) {
                continue;
            }
            src_idx = i - (uint32_t)delay;
            sum += channels[ch][src_idx];
        }
        out[i] = sum / (float)num_channels;
    }
}

#define MVDR_MAX_CHANNELS  4u
#define MVDR_REF_FREQ_HZ   1000.0f

void bm_algo_mvdr_beamform(const float * const *channels,
                           const int32_t *delay_samples,
                           uint32_t num_channels,
                           uint32_t n,
                           const bm_algo_mvdr_config_t *config,
                           float *out) {
    float var[MVDR_MAX_CHANNELS];
    float weight[MVDR_MAX_CHANNELS];
    float load;
    float sample_hz;
    float denom;
    uint32_t ch;
    uint32_t i;

    if (channels == NULL || delay_samples == NULL || out == NULL ||
        config == NULL || num_channels == 0u || num_channels > MVDR_MAX_CHANNELS ||
        n == 0u) {
        return;
    }

    load = (config->diagonal_load > 0.0f) ? config->diagonal_load : 1e-3f;
    sample_hz = (config->sample_hz > 0.0f) ? config->sample_hz : 8000.0f;
    denom = 0.0f;

    for (ch = 0u; ch < num_channels; ++ch) {
        float sum_sq = 0.0f;
        uint32_t count = 0u;
        int32_t delay = delay_samples[ch];
        float steer;
        float phase;

        if (delay < 0) {
            delay = 0;
        }
        if (channels[ch] == NULL) {
            var[ch] = load;
            weight[ch] = 0.0f;
            continue;
        }

        for (i = 0u; i < n; ++i) {
            uint32_t src_idx;

            if ((uint32_t)delay > i) {
                continue;
            }
            src_idx = i - (uint32_t)delay;
            sum_sq += channels[ch][src_idx] * channels[ch][src_idx];
            count++;
        }

        var[ch] = (count > 0u) ? (sum_sq / (float)count) + load : load;
        phase = 6.283185307f * MVDR_REF_FREQ_HZ * (float)delay / sample_hz;
        steer = cosf(phase);
        weight[ch] = (steer * steer) / var[ch];
        denom += weight[ch];
    }

    if (denom < 1e-12f) {
        bm_algo_delay_and_sum(channels, delay_samples, num_channels, n, out);
        return;
    }

    for (ch = 0u; ch < num_channels; ++ch) {
        weight[ch] /= denom;
    }

    for (i = 0u; i < n; ++i) {
        float sum = 0.0f;

        for (ch = 0u; ch < num_channels; ++ch) {
            int32_t delay = delay_samples[ch];
            uint32_t src_idx;
            float x;

            if (channels[ch] == NULL) {
                continue;
            }
            if (delay < 0) {
                delay = 0;
            }
            if ((uint32_t)delay > i) {
                continue;
            }
            src_idx = i - (uint32_t)delay;
            x = channels[ch][src_idx];
            sum += weight[ch] * x;
        }
        out[i] = sum;
    }
}

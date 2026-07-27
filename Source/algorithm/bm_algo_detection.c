/**
 * @file bm_algo_detection.c
 * @brief 检测算法实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-13       1.0            zeh            增加超声 ToF 检测
 * 2026-06-23       1.0            zeh            补齐 Doxygen 注释
 * 2026-07-27       1.1            zeh            matched_filter_feed 的 alpha
 *                                                改用 bm_algo_lpf1_alpha_saturate
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_detection.h"
#include "bm/algorithm/bm_algo_errors.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <float.h>
#include <math.h>

float bm_algo_matched_filter(const float *signal,
                             uint32_t signal_len,
                             const float *template,
                             uint32_t template_len,
                             uint32_t *best_index) {
    uint32_t i;
    float best = -FLT_MAX;
    uint32_t best_i = 0u;

    if (signal == NULL || template == NULL || template_len == 0u ||
        signal_len < template_len) {
        if (best_index != NULL) {
            *best_index = 0u;
        }
        return 0.0f;
    }

    for (i = 0u; i + template_len <= signal_len; ++i) {
        uint32_t k;
        float corr = 0.0f;
        int corr_finite = 1;

        for (k = 0u; k < template_len; ++k) {
            float s = signal[i + k];
            float t = template[k];

            /* Batch-3：模板含 NaN 时会返回看似合法的极差匹配；传播 NaN */
            if (!bm_algo_is_finite_f(s) || !bm_algo_is_finite_f(t)) {
                corr_finite = 0;
                break;
            }
            corr += s * t;
        }
        if (!corr_finite) {
            if (best_index != NULL) {
                *best_index = 0u;
            }
            return NAN;
        }
        if (corr > best) {
            best = corr;
            best_i = i;
        }
    }

    if (best_index != NULL) {
        *best_index = best_i;
    }
    return best;
}

void bm_algo_sync_demod_reset(bm_algo_sync_demod_state_t *state) {
    if (state != NULL) {
        state->i_accum = 0.0f;
        state->q_accum = 0.0f;
        state->alpha = 0.1f;
        state->count = 0u;
    }
}

void bm_algo_sync_demod_feed(bm_algo_sync_demod_state_t *state,
                             float sample,
                             float ref_sin,
                             float ref_cos) {
    float i_inst;
    float q_inst;

    if (state == NULL) {
        return;
    }

    {
        float alpha = bm_algo_lpf1_alpha_saturate(state->alpha);

        i_inst = sample * ref_cos;
        q_inst = sample * ref_sin;
        state->i_accum += alpha * (i_inst - state->i_accum);
        state->q_accum += alpha * (q_inst - state->q_accum);
    }
    state->count++;
}

float bm_algo_sync_demod_magnitude(const bm_algo_sync_demod_state_t *state) {
    if (state == NULL) {
        return 0.0f;
    }
    return sqrtf(state->i_accum * state->i_accum +
                 state->q_accum * state->q_accum);
}

int32_t bm_algo_ultrasonic_tof(const float *echo,
                               uint32_t n,
                               uint32_t min_delay,
                               float threshold,
                               float envelope_alpha) {
    uint32_t i;
    float envelope = 0.0f;
    float alpha;
    /* 未检测到时的哨兵值，语义同 BM_ALGO_ERR_NOT_FOUND（数值 -1）。 */
    int32_t peak_idx = BM_ALGO_ERR_NOT_FOUND;

    if (echo == NULL || n == 0u || min_delay >= n) {
        return BM_ALGO_ERR_INVALID;
    }

    alpha = envelope_alpha;
    if (alpha <= 0.0f) {
        alpha = 0.1f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    for (i = min_delay; i < n; ++i) {
        float abs_in = fabsf(echo[i]);

        envelope += alpha * (abs_in - envelope);
        if (peak_idx < 0 && envelope >= threshold) {
            peak_idx = (int32_t)i;
            break;
        }
    }

    return peak_idx;
}

/**
 * @file bm_algo_statistics.c
 * @brief 统计量实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_statistics.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

/**
 * @brief 复位统计状态，清零所有累积量
 *
 * @param state 统计状态指针，为 NULL 时静默返回
 */
void bm_algo_stats_reset(bm_algo_stats_state_t *state) {
    if (state == NULL) {
        return;
    }
    state->sum = 0.0f;
    state->sum_sq = 0.0f;
    state->mean = 0.0f;
    state->m2 = 0.0f;
    state->min_v = 0.0f;
    state->max_v = 0.0f;
    state->count = 0u;
}

/**
 * @brief 向统计器推入一个新样本（Welford 在线算法）
 *
 * 算法要点：
 *   delta  = value - mean_old
 *   mean  += delta / count
 *   delta2 = value - mean_new
 *   m2    += delta * delta2
 *
 * @param state 统计状态指针，为 NULL 时静默返回
 * @param value 新输入样本值；非有限值或 count 溢出时忽略
 */
void bm_algo_stats_push(bm_algo_stats_state_t *state, float value) {
    float delta;
    float delta2;

    if (state == NULL) {
        return;
    }
    if (state->count == UINT32_MAX || !bm_algo_is_finite_f(value)) {
        return;
    }

    if (state->count == 0u) {
        state->min_v = value;
        state->max_v = value;
    } else {
        if (value < state->min_v) {
            state->min_v = value;
        }
        if (value > state->max_v) {
            state->max_v = value;
        }
    }

    state->sum += value;
    state->sum_sq += value * value;
    state->count++;
    delta = value - state->mean;
    state->mean += delta / (float)state->count;
    delta2 = value - state->mean;
    state->m2 += delta * delta2;
}

/**
 * @brief 返回当前样本均值
 *
 * @param state 统计状态指针（const）
 * @return 均值；state 为 NULL 或 count 为 0 时返回 0.0f
 */
float bm_algo_stats_mean(const bm_algo_stats_state_t *state) {
    if (state == NULL || state->count == 0u) {
        return 0.0f;
    }
    return state->mean;
}

/**
 * @brief 返回当前总体方差（除以 N）
 *
 * @param state 统计状态指针（const）
 * @return 总体方差；state 为 NULL 或 count < 2 时返回 0.0f
 */
float bm_algo_stats_variance(const bm_algo_stats_state_t *state) {
    if (state == NULL || state->count < 2u) {
        return 0.0f;
    }

    return state->m2 / (float)state->count;
}

/**
 * @brief 返回均方根（RMS）值
 *
 * RMS = sqrt(sum_sq / count)
 *
 * @param state 统计状态指针（const）
 * @return RMS 值；state 为 NULL 或 count 为 0 时返回 0.0f
 */
float bm_algo_stats_rms(const bm_algo_stats_state_t *state) {
    if (state == NULL || state->count == 0u) {
        return 0.0f;
    }
    return sqrtf(state->sum_sq / (float)state->count);
}

/**
 * @brief 返回波峰因数（峰值 / RMS）
 *
 * 峰值 = max(|min_v|, |max_v|)；RMS 接近零（< 1e-12）时返回 0.0f。
 *
 * @param state 统计状态指针（const）
 * @return 波峰因数；state 为 NULL 或 count 为 0 时返回 0.0f
 */
float bm_algo_stats_crest_factor(const bm_algo_stats_state_t *state) {
    float rms;
    float peak;

    if (state == NULL || state->count == 0u) {
        return 0.0f;
    }

    rms = bm_algo_stats_rms(state);
    peak = fmaxf(fabsf(state->min_v), fabsf(state->max_v));
    if (rms < 1e-12f) {
        return 0.0f;
    }
    return peak / rms;
}

/**
 * @brief 计算浮点数组的算术均值
 *
 * @param data 输入数组指针
 * @param n    数组元素个数
 * @return 均值；data 为 NULL 或 n 为 0 时返回 0.0f
 */
float bm_algo_array_mean(const float *data, uint32_t n) {
    float sum = 0.0f;
    uint32_t i;

    if (data == NULL || n == 0u) {
        return 0.0f;
    }

    for (i = 0u; i < n; ++i) {
        sum += data[i];
    }
    return sum / (float)n;
}

/**
 * @brief 计算浮点数组的均方根（RMS）
 *
 * @param data 输入数组指针
 * @param n    数组元素个数
 * @return RMS 值；data 为 NULL 或 n 为 0 时返回 0.0f
 */
float bm_algo_array_rms(const float *data, uint32_t n) {
    float sum_sq = 0.0f;
    uint32_t i;

    if (data == NULL || n == 0u) {
        return 0.0f;
    }

    for (i = 0u; i < n; ++i) {
        sum_sq += data[i] * data[i];
    }
    return sqrtf(sum_sq / (float)n);
}

/**
 * @brief 计算浮点数组的绝对值峰值
 *
 * @param data 输入数组指针
 * @param n    数组元素个数
 * @return max(|data[i]|)；data 为 NULL 或 n 为 0 时返回 0.0f
 */
float bm_algo_array_peak(const float *data, uint32_t n) {
    float peak = 0.0f;
    uint32_t i;

    if (data == NULL || n == 0u) {
        return 0.0f;
    }

    for (i = 0u; i < n; ++i) {
        float a = fabsf(data[i]);
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

/**
 * @brief 复位变化率估算器，设置初始输入值
 *
 * @param state 估算器状态指针，为 NULL 时静默返回
 * @param input 初始输入值（用于下次步进的基准）
 */
void bm_algo_rate_est_reset(bm_algo_rate_est_state_t *state, float input) {
    if (state == NULL) {
        return;
    }
    state->prev_input = input;
    state->rate_per_s = 0.0f;
}

/**
 * @brief 推进变化率估算一步
 *
 * rate = (input - prev_input) / dt_s
 *
 * @param state  估算器状态指针，为 NULL 时返回 0.0f
 * @param input  当前输入值
 * @param dt_s   时间步长（秒），须 > 0；否则返回上次结果
 * @return 本步估算的变化率（单位/秒）
 */
float bm_algo_rate_est_step(bm_algo_rate_est_state_t *state,
                            float input,
                            float dt_s) {
    if (state == NULL || dt_s <= 0.0f) {
        return state != NULL ? state->rate_per_s : 0.0f;
    }

    state->rate_per_s = (input - state->prev_input) / dt_s;
    state->prev_input = input;
    return state->rate_per_s;
}

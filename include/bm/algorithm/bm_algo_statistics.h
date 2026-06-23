/**
 * @file bm_algo_statistics.h
 * @brief 统计量：均值、方差、RMS、峰值与波峰因数
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
#ifndef BM_ALGO_STATISTICS_H
#define BM_ALGO_STATISTICS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 在线统计累积状态
 *
 * 使用 Welford 算法维护均值与二阶矩，可同时追踪 RMS、最值。
 */
typedef struct {
    float    sum;     /**< 样本累加和 */
    float    sum_sq;  /**< 样本平方累加和（用于 RMS） */
    float    mean;    /**< 当前均值（Welford 在线更新） */
    float    m2;      /**< 方差二阶矩累加量（Welford） */
    float    min_v;   /**< 历史最小值 */
    float    max_v;   /**< 历史最大值 */
    uint32_t count;   /**< 已接受样本数 */
} bm_algo_stats_state_t;

/**
 * @brief 复位统计状态，清零所有累积量
 *
 * @param state 统计状态指针，为 NULL 时静默返回
 */
void bm_algo_stats_reset(bm_algo_stats_state_t *state);

/**
 * @brief 向统计器推入一个新样本
 *
 * 采用 Welford 在线算法更新均值与二阶矩；同步更新 sum、sum_sq 及最值。
 * 若 value 为非有限值（NaN/Inf）或 count 已达 UINT32_MAX，则忽略该样本。
 *
 * @param state 统计状态指针，为 NULL 时静默返回
 * @param value 新输入样本值
 */
void bm_algo_stats_push(bm_algo_stats_state_t *state, float value);

/**
 * @brief 返回当前样本均值
 *
 * @param state 统计状态指针（const）
 * @return 均值；state 为 NULL 或 count 为 0 时返回 0.0f
 */
float bm_algo_stats_mean(const bm_algo_stats_state_t *state);

/**
 * @brief 返回当前总体方差（除以 N，非 N-1）
 *
 * @param state 统计状态指针（const）
 * @return 总体方差；state 为 NULL 或 count < 2 时返回 0.0f
 */
float bm_algo_stats_variance(const bm_algo_stats_state_t *state);

/**
 * @brief 返回当前均方根（RMS）值
 *
 * @param state 统计状态指针（const）
 * @return RMS 值；state 为 NULL 或 count 为 0 时返回 0.0f
 */
float bm_algo_stats_rms(const bm_algo_stats_state_t *state);

/**
 * @brief 返回波峰因数（峰值 / RMS）
 *
 * 峰值取 max(|min_v|, |max_v|)。RMS 接近零时返回 0.0f 以避免除以零。
 *
 * @param state 统计状态指针（const）
 * @return 波峰因数；state 为 NULL 或 count 为 0 时返回 0.0f
 */
float bm_algo_stats_crest_factor(const bm_algo_stats_state_t *state);

/**
 * @brief 计算浮点数组的算术均值
 *
 * @param data 输入数组指针
 * @param n    数组元素个数
 * @return 均值；data 为 NULL 或 n 为 0 时返回 0.0f
 */
float bm_algo_array_mean(const float *data, uint32_t n);

/**
 * @brief 计算浮点数组的均方根（RMS）
 *
 * @param data 输入数组指针
 * @param n    数组元素个数
 * @return RMS 值；data 为 NULL 或 n 为 0 时返回 0.0f
 */
float bm_algo_array_rms(const float *data, uint32_t n);

/**
 * @brief 计算浮点数组的绝对值峰值
 *
 * @param data 输入数组指针
 * @param n    数组元素个数
 * @return max(|data[i]|)；data 为 NULL 或 n 为 0 时返回 0.0f
 */
float bm_algo_array_peak(const float *data, uint32_t n);

/**
 * @brief 一阶变化率估算状态（输出单位/秒）
 */
typedef struct {
    float prev_input;  /**< 上一拍输入值 */
    float rate_per_s;  /**< 最近估算的变化率（单位/秒） */
} bm_algo_rate_est_state_t;

/**
 * @brief 复位变化率估算器
 *
 * @param state 估算器状态指针，为 NULL 时静默返回
 * @param input 初始输入值（用于下次步进的基准）
 */
void bm_algo_rate_est_reset(bm_algo_rate_est_state_t *state, float input);

/**
 * @brief 推进变化率估算一步
 *
 * rate = (input - prev_input) / dt_s
 *
 * @param state  估算器状态指针，为 NULL 时返回 0.0f
 * @param input  当前输入值
 * @param dt_s   距上次调用的时间步长（秒），须 > 0
 * @return 本步估算的变化率（单位/秒）；dt_s <= 0 时返回上次结果
 */
float bm_algo_rate_est_step(bm_algo_rate_est_state_t *state,
                            float input,
                            float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_STATISTICS_H */

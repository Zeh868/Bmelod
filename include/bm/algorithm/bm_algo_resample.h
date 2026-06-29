/**
 * @file bm_algo_resample.h
 * @brief 重采样：抽取、线性插值
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            时钟漂移跟踪/补偿
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_RESAMPLE_H
#define BM_ALGO_RESAMPLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 整数抽取状态（每 decim 个输入取 1 个输出）
 */
typedef struct {
    uint32_t decim;   /**< 当前抽取因子 */
    uint32_t counter; /**< 剩余跳过样本数 */
} bm_algo_decimator_state_t;

/**
 * @brief 重置抽取器状态（计数器清零，下一个输入样本将直接通过）
 *
 * @param state 抽取器状态（不可为 NULL）
 */
void bm_algo_decimator_reset(bm_algo_decimator_state_t *state);

/**
 * @brief 整数抽取单步：每 decim 个输入通过 1 个
 *
 * @param state  抽取器状态（不可为 NULL）
 * @param decim  抽取因子（须 ≥1）
 * @param input  当前输入样本
 * @param output 输出样本指针（通过时写入，可为 NULL）
 * @return 1 本样本通过；0 本样本被丢弃
 */
int bm_algo_decimator_step(bm_algo_decimator_state_t *state,
                           uint32_t decim,
                           float input,
                           float *output);

/**
 * @brief 线性插值重采样状态（固定输出/输入比率）
 */
typedef struct {
    float ratio;        /**< 输出/输入采样率比（须 >0） */
    float phase;        /**< 当前相位（0..1 之间，表示当前输入区间内的插值位置） */
    float prev_sample;  /**< 上一个输入样本（用于插值） */
} bm_algo_linear_resampler_state_t;

/**
 * @brief 重置线性重采样器状态
 *
 * @param state   重采样器状态（不可为 NULL）
 * @param ratio   输出/输入采样率比（>0 且有限；否则置为 0 禁用）
 * @param initial 初始前一样本值（用于首次插值）
 */
void bm_algo_linear_resampler_reset(bm_algo_linear_resampler_state_t *state,
                                    float ratio,
                                    float initial);

/**
 * @brief 线性重采样单步：输入 1 个样本，输出 0 到多个插值样本
 *
 * 相位步长 step=1/ratio；每次 phase≤1 时产生一个插值输出并推进 phase。
 * 当 max_outputs 不足以容纳本次产生的所有样本时返回 -1，不消耗输入。
 *
 * @param state       重采样器状态（不可为 NULL）
 * @param input       当前输入样本
 * @param outputs     输出缓冲（不可为 NULL）
 * @param max_outputs 输出缓冲容量（须 ≥1）
 * @param out_count   实际输出样本数（不可为 NULL）
 * @return 实际输出数（≥0）；-1 缓冲不足或参数非法（此时不消耗输入）
 */
int bm_algo_linear_resampler_step(bm_algo_linear_resampler_state_t *state,
                                  float input,
                                  float *outputs,
                                  uint32_t max_outputs,
                                  uint32_t *out_count);

/* ---------- 多相 FIR 抽取 ---------- */

/**
 * @brief 多相 FIR 抽取配置
 */
typedef struct {
    const float *coeffs;  /**< FIR 系数数组（低通原型），长度 tap_count */
    uint32_t tap_count;   /**< FIR 阶数（滤波器系数个数，须 ≥1） */
    uint32_t decim;       /**< 抽取因子（须 ≥1） */
} bm_algo_polyphase_decim_config_t;

/**
 * @brief 多相 FIR 抽取运行状态（内部字段，勿直接修改）
 */
typedef struct {
    float    *delay_line;     /**< 外部提供的循环延迟线缓冲 */
    uint32_t  delay_len;      /**< delay_line 元素个数 */
    uint32_t  tap_count;      /**< 初始化时锁定的 FIR 阶数 */
    uint32_t  decim;          /**< 初始化时锁定的抽取因子 */
    uint32_t  index;          /**< 延迟线写指针（循环） */
    uint32_t  decim_counter;  /**< 抽取计数器（0 时产生输出） */
} bm_algo_polyphase_decim_state_t;

/**
 * @brief 初始化多相 FIR 抽取器（绑定延迟线并清零）
 *
 * @param state      抽取器状态（不可为 NULL）
 * @param config     配置（不可为 NULL，coeffs 不可为 NULL，tap_count/decim须>0）
 * @param delay_line 外部提供的延迟线缓冲（须 ≥tap_count 个 float）
 * @param delay_len  delay_line 元素个数
 * @return 0 成功；-1 参数非法
 */
int bm_algo_polyphase_decim_init(bm_algo_polyphase_decim_state_t *state,
                                 const bm_algo_polyphase_decim_config_t *config,
                                 float *delay_line,
                                 uint32_t delay_len);

/**
 * @brief 重置多相 FIR 抽取器（清零延迟线和计数器）
 *
 * @param state  抽取器状态（不可为 NULL）
 * @param config 配置（保留参数，当前实现未使用，可传 NULL）
 */
void bm_algo_polyphase_decim_reset(bm_algo_polyphase_decim_state_t *state,
                                   const bm_algo_polyphase_decim_config_t *config);

/**
 * @brief 多相 FIR 抽取块处理：输入 in_count 个样本，输出最多 in_count/decim 个
 *
 * 每输入 decim 个样本计算一次全阶 FIR 卷积（循环缓冲实现），产生 1 个输出。
 *
 * @param state    抽取器状态（不可为 NULL，须先调用 init）
 * @param config   配置（须与 init 时一致）
 * @param in       输入样本数组，长度 in_count
 * @param out      输出样本数组（容量须 ≥ ceil(in_count/decim)）
 * @param in_count 输入样本数
 * @return 实际输出样本数；参数非法时返回 0
 */
uint32_t bm_algo_polyphase_decim_process(bm_algo_polyphase_decim_state_t *state,
                                         const bm_algo_polyphase_decim_config_t *config,
                                         const float *in,
                                         float *out,
                                         uint32_t in_count);

/* ---------- 时钟漂移跟踪（alpha 一阶 IIR 偏差估计） ---------- */

/**
 * @brief 时钟漂移跟踪配置
 */
typedef struct {
    float alpha; /**< 一阶 IIR 滤波系数（(0,1]，越大跟踪越快但噪声越大） */
} bm_algo_clock_drift_config_t;

/**
 * @brief 时钟漂移跟踪状态
 */
typedef struct {
    float ratio_error; /**< 当前估计的漂移比（actual/expected - 1.0） */
} bm_algo_clock_drift_state_t;

/**
 * @brief 重置时钟漂移状态（ratio_error 归零）
 *
 * @param state 漂移状态（不可为 NULL）
 */
void bm_algo_clock_drift_reset(bm_algo_clock_drift_state_t *state);

/**
 * @brief 用期望/实际间隔更新漂移比估计
 *
 * @param state 漂移状态
 * @param config alpha 滤波系数（0,1]
 * @param expected_dt_s 标称间隔（秒）
 * @param actual_dt_s 实测间隔（秒）
 */
void bm_algo_clock_drift_feed(bm_algo_clock_drift_state_t *state,
                              const bm_algo_clock_drift_config_t *config,
                              float expected_dt_s,
                              float actual_dt_s);

/**
 * @brief 补偿实测间隔
 *
 * @param state 漂移状态（可为 NULL，返回 actual_dt_s）
 * @param actual_dt_s 实测间隔（秒）
 * @return 校正后间隔（秒）
 */
float bm_algo_clock_drift_compensate(const bm_algo_clock_drift_state_t *state,
                                     float actual_dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_RESAMPLE_H */

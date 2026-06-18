/**
 * @file bm_algo_common.h
 * @brief 算法公共工具：饱和、死区、滞环、速率限制与角度归一化
 *
 * 纯数学核，无框架状态与 HAL 依赖。所有函数支持多实例并发调用。
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
#ifndef BM_ALGO_COMMON_H
#define BM_ALGO_COMMON_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 将 value 限制在 [min_v, max_v] 区间 */
float bm_algo_clamp_f(float value, float min_v, float max_v);

/** 对称饱和：限制在 [-limit, limit] */
float bm_algo_saturate_f(float value, float limit);

/** 死区：|value| < width 时输出 0，否则减去死区偏移 */
float bm_algo_deadband_f(float value, float width);

typedef struct {
    float low_threshold;
    float high_threshold;
    int   output_high;
} bm_algo_hysteresis_config_t;

typedef struct {
    int latch;
} bm_algo_hysteresis_state_t;

void bm_algo_hysteresis_reset(bm_algo_hysteresis_state_t *state, int output_high);
float bm_algo_hysteresis_step(bm_algo_hysteresis_state_t *state,
                              const bm_algo_hysteresis_config_t *config,
                              float value);

typedef struct {
    float max_rise_per_s;
    float max_fall_per_s;
} bm_algo_rate_limit_config_t;

typedef struct {
    float output;
} bm_algo_rate_limit_state_t;

void bm_algo_rate_limit_reset(bm_algo_rate_limit_state_t *state, float output);
float bm_algo_rate_limit_step(bm_algo_rate_limit_state_t *state,
                              const bm_algo_rate_limit_config_t *config,
                              float target,
                              float dt_s);

/** 将角度归一化到 [-pi, pi)（弧度） */
float bm_algo_angle_wrap_rad(float angle_rad);

/** 将角度归一化到 [0, 2*pi)（弧度） */
float bm_algo_angle_wrap_0_2pi_rad(float angle_rad);

/** 两角度最短差（弧度，结果在 [-pi, pi)） */
float bm_algo_angle_delta_rad(float from_rad, float to_rad);

/** 检查浮点是否为有限值（非 NaN/Inf） */
int bm_algo_is_finite_f(float value);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_COMMON_H */

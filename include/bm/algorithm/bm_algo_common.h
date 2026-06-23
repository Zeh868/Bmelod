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

/**
 * @brief 将浮点值钳位到指定区间 [min_v, max_v]
 *
 * @param value 输入值
 * @param min_v 下限
 * @param max_v 上限（须 >= min_v，否则行为未定义）
 * @return 钳位后的值
 */
float bm_algo_clamp_f(float value, float min_v, float max_v);

/**
 * @brief 对称饱和：将值限制在 [-limit, limit]
 *
 * @param value 输入值
 * @param limit 饱和限幅（须 >= 0）
 * @return 限幅后的值
 */
float bm_algo_saturate_f(float value, float limit);

/**
 * @brief 死区处理：|value| < width 时输出 0，否则减去死区偏移
 *
 * width <= 0 时直通返回原值。
 *
 * @param value 输入值
 * @param width 死区半宽（>= 0）
 * @return 死区处理后的值
 */
float bm_algo_deadband_f(float value, float width);

/**
 * @brief 滞环比较器配置参数
 */
typedef struct {
    float low_threshold;  /**< 下降沿阈值：latch 为高时，低于此值则切换为低 */
    float high_threshold; /**< 上升沿阈值：latch 为低时，高于此值则切换为高 */
    int   output_high;    /**< 保留字段（复位参数由 bm_algo_hysteresis_reset 传入） */
} bm_algo_hysteresis_config_t;

/**
 * @brief 滞环比较器状态
 */
typedef struct {
    int latch; /**< 当前输出锁存值：1 = 高，0 = 低 */
} bm_algo_hysteresis_state_t;

/**
 * @brief 复位滞环比较器状态
 *
 * @param state       状态指针，为 NULL 时静默返回
 * @param output_high 初始锁存输出：非 0 表示高，0 表示低
 */
void bm_algo_hysteresis_reset(bm_algo_hysteresis_state_t *state, int output_high);

/**
 * @brief 滞环比较器步进运算
 *
 * 根据当前 latch 状态与 config 中的阈值决定是否翻转输出。
 *
 * @param state  状态指针
 * @param config 滞环配置指针
 * @param value  当前输入值
 * @return 输出值：1.0f（高）或 0.0f（低）；state/config 为 NULL 时直通返回 value
 */
float bm_algo_hysteresis_step(bm_algo_hysteresis_state_t *state,
                              const bm_algo_hysteresis_config_t *config,
                              float value);

/**
 * @brief 速率限制器配置参数
 */
typedef struct {
    float max_rise_per_s; /**< 允许的最大上升速率（单位/秒） */
    float max_fall_per_s; /**< 允许的最大下降速率（单位/秒，正值） */
} bm_algo_rate_limit_config_t;

/**
 * @brief 速率限制器状态
 */
typedef struct {
    float output; /**< 上一拍限速后的输出值 */
} bm_algo_rate_limit_state_t;

/**
 * @brief 复位速率限制器，设置初始输出值
 *
 * @param state  状态指针，为 NULL 时静默返回
 * @param output 初始输出值
 */
void bm_algo_rate_limit_reset(bm_algo_rate_limit_state_t *state, float output);

/**
 * @brief 速率限制器步进运算
 *
 * 对 target 相对上一输出的变化量施加上/下行速率限制。
 *
 * @param state  状态指针
 * @param config 速率限制配置指针
 * @param target 本步期望目标值
 * @param dt_s   时间步长（秒），须 > 0
 * @return 限速后的输出值；state/config 为 NULL 或 dt_s <= 0 时直通返回 target
 */
float bm_algo_rate_limit_step(bm_algo_rate_limit_state_t *state,
                              const bm_algo_rate_limit_config_t *config,
                              float target,
                              float dt_s);

/**
 * @brief 将弧度角归一化到 [-π, π)
 *
 * 非有限输入（NaN/Inf）直通返回。
 *
 * @param angle_rad 输入角度（弧度）
 * @return 归一化后的角度，范围 [-π, π)
 */
float bm_algo_angle_wrap_rad(float angle_rad);

/**
 * @brief 将弧度角归一化到 [0, 2π)
 *
 * 非有限输入（NaN/Inf）直通返回。
 *
 * @param angle_rad 输入角度（弧度）
 * @return 归一化后的角度，范围 [0, 2π)
 */
float bm_algo_angle_wrap_0_2pi_rad(float angle_rad);

/**
 * @brief 计算两角度的最短差（弧度），结果在 [-π, π)
 *
 * @param from_rad 起始角度（弧度）
 * @param to_rad   目标角度（弧度）
 * @return to_rad - from_rad 的归一化差值，范围 [-π, π)
 */
float bm_algo_angle_delta_rad(float from_rad, float to_rad);

/**
 * @brief 检查浮点值是否为有限值（非 NaN、非 Inf）
 *
 * @param value 待检查的浮点值
 * @return 非 0 表示有限；0 表示 NaN 或 Inf
 */
int bm_algo_is_finite_f(float value);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_COMMON_H */

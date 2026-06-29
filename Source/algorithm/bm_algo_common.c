/**
 * @file bm_algo_common.c
 * @brief 算法公共工具实现
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
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

/**
 * @brief 将浮点值钳位到指定区间 [min_v, max_v]
 *
 * @param value 输入值
 * @param min_v 下限
 * @param max_v 上限
 * @return 钳位后的值
 */
float bm_algo_clamp_f(float value, float min_v, float max_v) {
    if (value < min_v) {
        return min_v;
    }
    if (value > max_v) {
        return max_v;
    }
    return value;
}

/**
 * @brief 对称饱和：将值限制在 [-limit, limit]
 *
 * @param value 输入值
 * @param limit 饱和限幅（须 >= 0）
 * @return 限幅后的值
 */
float bm_algo_saturate_f(float value, float limit) {
    return bm_algo_clamp_f(value, -limit, limit);
}

/**
 * @brief 死区处理：|value| < width 时输出 0，否则减去死区偏移
 *
 * @param value 输入值
 * @param width 死区半宽（>= 0）；<= 0 时直通返回原值
 * @return 死区处理后的值
 */
float bm_algo_deadband_f(float value, float width) {
    if (width <= 0.0f) {
        return value;
    }
    if (value > width) {
        return value - width;
    }
    if (value < -width) {
        return value + width;
    }
    return 0.0f;
}

/**
 * @brief 复位滞环比较器状态
 *
 * @param state       状态指针，为 NULL 时静默返回
 * @param output_high 初始锁存输出：非 0 为高，0 为低
 */
void bm_algo_hysteresis_reset(bm_algo_hysteresis_state_t *state, int output_high) {
    if (state != NULL) {
        state->latch = output_high;
    }
}

/**
 * @brief 滞环比较器步进运算
 *
 * latch 为高时，value < low_threshold 则切换为低；
 * latch 为低时，value > high_threshold 则切换为高。
 *
 * @param state  状态指针
 * @param config 滞环配置指针（包含上下阈值）
 * @param value  当前输入值
 * @return 1.0f（输出高）或 0.0f（输出低）；state/config 为 NULL 时直通返回 value
 */
float bm_algo_hysteresis_step(bm_algo_hysteresis_state_t *state,
                              const bm_algo_hysteresis_config_t *config,
                              float value) {
    if (state == NULL || config == NULL) {
        return value;
    }
    if (state->latch) {
        if (value < config->low_threshold) {
            state->latch = 0;
        }
    } else {
        if (value > config->high_threshold) {
            state->latch = 1;
        }
    }
    return state->latch ? 1.0f : 0.0f;
}

/**
 * @brief 复位速率限制器，设置初始输出值
 *
 * @param state  状态指针，为 NULL 时静默返回
 * @param output 初始输出值
 */
void bm_algo_rate_limit_reset(bm_algo_rate_limit_state_t *state, float output) {
    if (state != NULL) {
        state->output = output;
    }
}

/**
 * @brief 速率限制器步进运算
 *
 * 计算 delta = target - output，然后钳位到 [-max_down*dt, max_up*dt]，
 * 再累加到 output。
 *
 * @param state  状态指针
 * @param config 速率配置指针（max_rise_per_s、max_fall_per_s）
 * @param target 本步期望目标值
 * @param dt_s   时间步长（秒），须 > 0
 * @return 限速后的输出值；state/config 为 NULL 或 dt_s <= 0 时直通返回 target
 */
float bm_algo_rate_limit_step(bm_algo_rate_limit_state_t *state,
                              const bm_algo_rate_limit_config_t *config,
                              float target,
                              float dt_s) {
    float delta;
    float max_up;
    float max_down;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return target;
    }

    delta = target - state->output;
    max_up = config->max_rise_per_s * dt_s;
    max_down = config->max_fall_per_s * dt_s;

    if (delta > max_up) {
        delta = max_up;
    } else if (delta < -max_down) {
        delta = -max_down;
    }

    state->output += delta;
    return state->output;
}

/**
 * @brief 将弧度角归一化到 [-π, π)
 *
 * 使用 fmodf 实现：先平移 +π 取模 2π，再减回 π；
 * 处理 fmodf 对负数返回负余数的情况。
 *
 * @param angle_rad 输入角度（弧度）
 * @return 归一化后的角度，范围 [-π, π)；非有限输入直通返回
 */
float bm_algo_angle_wrap_rad(float angle_rad) {
    const float two_pi = 2.0f * BM_ALGO_PI_F;

    if (!bm_algo_is_finite_f(angle_rad)) {
        return angle_rad;
    }
    angle_rad = fmodf(angle_rad + BM_ALGO_PI_F, two_pi);
    if (angle_rad < 0.0f) {
        angle_rad += two_pi;
    }
    return angle_rad - BM_ALGO_PI_F;
}

/**
 * @brief 将弧度角归一化到 [0, 2π)
 *
 * @param angle_rad 输入角度（弧度）
 * @return 归一化后的角度，范围 [0, 2π)；非有限输入直通返回
 */
float bm_algo_angle_wrap_0_2pi_rad(float angle_rad) {
    const float two_pi = 2.0f * BM_ALGO_PI_F;

    if (!bm_algo_is_finite_f(angle_rad)) {
        return angle_rad;
    }
    angle_rad = fmodf(angle_rad, two_pi);
    if (angle_rad < 0.0f) {
        angle_rad += two_pi;
    }
    return angle_rad;
}

/**
 * @brief 计算两角度的最短差（弧度），结果在 [-π, π)
 *
 * @param from_rad 起始角度（弧度）
 * @param to_rad   目标角度（弧度）
 * @return 归一化后的差值 to_rad - from_rad，范围 [-π, π)
 */
float bm_algo_angle_delta_rad(float from_rad, float to_rad) {
    return bm_algo_angle_wrap_rad(to_rad - from_rad);
}

/**
 * @brief 检查浮点值是否为有限值（非 NaN、非 Inf）
 *
 * 优先使用标准库宏 isfinite；不可用时退回位模式判断。
 *
 * @param value 待检查的浮点值
 * @return 非 0 表示有限；0 表示 NaN 或 Inf
 */
int bm_algo_is_finite_f(float value) {
#if defined(isfinite)
    return isfinite(value) != 0;
#else
    return (value == value) && (value * 0.0f == 0.0f);
#endif
}

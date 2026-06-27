/**
 * @file bm_algo_profile.h
 * @brief 轨迹规划：斜坡、梯形速度曲线与 S 曲线
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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_PROFILE_H
#define BM_ALGO_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 斜坡 ---------- */

/**
 * @brief 斜坡限速配置
 */
typedef struct {
    float rate_per_s;  /**< 最大变化率（单位/秒），须 > 0 */
} bm_algo_ramp_config_t;

/**
 * @brief 斜坡限速状态
 */
typedef struct {
    float output; /**< 当前输出值 */
    int   done;   /**< 非零表示已到达目标 */
} bm_algo_ramp_state_t;

/**
 * @brief 复位斜坡限速状态
 *
 * @param state  斜坡状态结构体，NULL 时静默返回
 * @param output 复位时的初始输出值
 */
void bm_algo_ramp_reset(bm_algo_ramp_state_t *state, float output);

/**
 * @brief 斜坡限速单步更新
 *
 * 每次以不超过 config->rate_per_s * dt_s 的步长向 target 逼近。
 * 到达目标后置 state->done = 1。
 *
 * @param state  斜坡状态结构体
 * @param config 斜坡配置（变化率）
 * @param target 本步目标值
 * @param dt_s   步长时间（秒），须 > 0；否则直接返回 target
 * @return 更新后的输出值
 */
float bm_algo_ramp_step(bm_algo_ramp_state_t *state,
                        const bm_algo_ramp_config_t *config,
                        float target,
                        float dt_s);

/* ---------- 梯形速度轨迹 ---------- */

/**
 * @brief 梯形速度轨迹配置
 */
typedef struct {
    float max_vel;   /**< 最大速度（单位/秒），须 > 0 */
    float max_accel; /**< 最大加速度（单位/秒²），须 > 0 */
    float max_decel; /**< 最大减速度（单位/秒²），须 > 0 */
} bm_algo_trapezoid_config_t;

/**
 * @brief 梯形速度轨迹状态
 */
typedef struct {
    float position; /**< 当前位置 */
    float velocity; /**< 当前速度（单位/秒） */
    float target;   /**< 目标位置 */
    int   phase;    /**< 运动阶段：0=加速 1=匀速 2=减速 3=完成 */
    int   done;     /**< 非零表示已到达目标 */
} bm_algo_trapezoid_state_t;

/**
 * @brief 复位梯形轨迹状态
 *
 * @param state    梯形轨迹状态结构体，NULL 时静默返回
 * @param position 复位时的初始位置
 * @param velocity 复位时的初始速度
 */
void bm_algo_trapezoid_reset(bm_algo_trapezoid_state_t *state,
                             float position,
                             float velocity);

/**
 * @brief 设置梯形轨迹目标位置并启动运动
 *
 * @param state  梯形轨迹状态结构体，NULL 时静默返回
 * @param target 新的目标位置
 */
void bm_algo_trapezoid_set_target(bm_algo_trapezoid_state_t *state, float target);

/**
 * @brief 梯形速度轨迹单步更新
 *
 * 根据当前位置、速度和剩余距离自动切换加速/匀速/减速阶段，
 * 过零点时强制对齐到目标并置 done。
 *
 * @param state  梯形轨迹状态结构体
 * @param config 梯形轨迹配置（速度、加减速限制）
 * @param dt_s   步长时间（秒），须 > 0；否则返回 0.0f
 * @return 更新后的当前位置
 */
float bm_algo_trapezoid_step(bm_algo_trapezoid_state_t *state,
                             const bm_algo_trapezoid_config_t *config,
                             float dt_s);

/* ---------- 七段 S 曲线（jerk 受限） ---------- */

/**
 * @brief S 曲线轨迹配置
 */
typedef struct {
    float max_vel;   /**< 最大速度（单位/秒），须 > 0 */
    float max_accel; /**< 最大加速度（单位/秒²），须 > 0 */
    float max_jerk;  /**< 最大加加速度 jerk（单位/秒³），须 > 0 */
} bm_algo_scurve_config_t;

/**
 * @brief S 曲线轨迹状态
 */
typedef struct {
    float position;     /**< 当前位置 */
    float velocity;     /**< 当前速度（单位/秒） */
    float acceleration; /**< 当前加速度（单位/秒²） */
    float target;       /**< 目标位置 */
    int   done;         /**< 非零表示已到达目标 */
} bm_algo_scurve_state_t;

/**
 * @brief 复位 S 曲线轨迹状态
 *
 * @param state        S 曲线状态结构体，NULL 时静默返回
 * @param position     复位时的初始位置
 * @param velocity     复位时的初始速度
 * @param acceleration 复位时的初始加速度
 */
void bm_algo_scurve_reset(bm_algo_scurve_state_t *state,
                          float position,
                          float velocity,
                          float acceleration);

/**
 * @brief 设置 S 曲线轨迹目标位置并启动运动
 *
 * @param state  S 曲线状态结构体，NULL 时静默返回
 * @param target 新的目标位置
 */
void bm_algo_scurve_set_target(bm_algo_scurve_state_t *state, float target);

/**
 * @brief S 曲线轨迹单步更新（jerk 受限制动距离控制器）
 *
 * 基于制动距离估算决定加速或减速，acceleration 变化量受 max_jerk * dt_s 限制，
 * 过零点时强制对齐到目标并置 done。
 *
 * @param state  S 曲线状态结构体
 * @param config S 曲线配置（速度、加速度、jerk 限制）
 * @param dt_s   步长时间（秒），须 > 0；否则返回 0.0f
 * @return 更新后的当前位置
 */
float bm_algo_scurve_step(bm_algo_scurve_state_t *state,
                          const bm_algo_scurve_config_t *config,
                          float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_PROFILE_H */

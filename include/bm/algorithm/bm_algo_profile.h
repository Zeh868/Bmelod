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
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_PROFILE_H
#define BM_ALGO_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 斜坡 ---------- */
typedef struct {
    float rate_per_s;  /**< 变化率（单位/秒） */
} bm_algo_ramp_config_t;

typedef struct {
    float output;
    int   done;
} bm_algo_ramp_state_t;

void bm_algo_ramp_reset(bm_algo_ramp_state_t *state, float output);
float bm_algo_ramp_step(bm_algo_ramp_state_t *state,
                        const bm_algo_ramp_config_t *config,
                        float target,
                        float dt_s);

/* ---------- 梯形速度轨迹 ---------- */
typedef struct {
    float max_vel;
    float max_accel;
    float max_decel;
} bm_algo_trapezoid_config_t;

typedef struct {
    float position;
    float velocity;
    float target;
    int   phase;   /* 0=加速 1=匀速 2=减速 3=完成 */
    int   done;
} bm_algo_trapezoid_state_t;

void bm_algo_trapezoid_reset(bm_algo_trapezoid_state_t *state,
                             float position,
                             float velocity);
void bm_algo_trapezoid_set_target(bm_algo_trapezoid_state_t *state, float target);
float bm_algo_trapezoid_step(bm_algo_trapezoid_state_t *state,
                             const bm_algo_trapezoid_config_t *config,
                             float dt_s);

/* ---------- 七段 S 曲线（jerk 受限） ---------- */
typedef struct {
    float max_vel;
    float max_accel;
    float max_jerk;
} bm_algo_scurve_config_t;

typedef struct {
    float position;
    float velocity;
    float acceleration;
    float target;
    int   done;
} bm_algo_scurve_state_t;

void bm_algo_scurve_reset(bm_algo_scurve_state_t *state,
                          float position,
                          float velocity,
                          float acceleration);
void bm_algo_scurve_set_target(bm_algo_scurve_state_t *state, float target);
float bm_algo_scurve_step(bm_algo_scurve_state_t *state,
                          const bm_algo_scurve_config_t *config,
                          float dt_s);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_PROFILE_H */

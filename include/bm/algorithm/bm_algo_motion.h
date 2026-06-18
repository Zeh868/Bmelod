/**
 * @file bm_algo_motion.h
 * @brief 运动辅助：编码器展开、速度估算与 DDA 插补
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
 * 2026-06-17       1.1            zeh            编码器 index/丢脉冲诊断
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_MOTION_H
#define BM_ALGO_MOTION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 编码器计数展开 ---------- */
typedef struct {
    uint32_t counts_per_rev;
    int32_t  prev_count;
    int32_t  turns;
    float    position_rad;
} bm_algo_encoder_config_t;

typedef struct {
    int32_t  prev_count;
    int32_t  turns;
    float    position_rad;
    float    velocity_rad_s;
} bm_algo_encoder_state_t;

void bm_algo_encoder_reset(bm_algo_encoder_state_t *state,
                           const bm_algo_encoder_config_t *config,
                           int32_t raw_count);
float bm_algo_encoder_update(bm_algo_encoder_state_t *state,
                             const bm_algo_encoder_config_t *config,
                             int32_t raw_count,
                             float dt_s);

/* ---------- DDA 直线插补（二维） ---------- */
typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
    float step_size;
} bm_algo_dda_config_t;

typedef struct {
    float x;
    float y;
    float err;
    int   done;
    int   step_x;
    int   step_y;
    float dx;
    float dy;
    float target_x;
    float target_y;
    float step_size;
    uint32_t steps;
    uint32_t step_count;
} bm_algo_dda_state_t;

void bm_algo_dda_reset(bm_algo_dda_state_t *state,
                       const bm_algo_dda_config_t *config);
int bm_algo_dda_step(bm_algo_dda_state_t *state,
                     const bm_algo_dda_config_t *config,
                     float *x_out,
                     float *y_out);

/* ---------- 步进脉冲生成（速度给定 → 步脉冲） ---------- */
typedef struct {
    float max_velocity_steps_s;
} bm_algo_stepper_config_t;

typedef struct {
    float phase;
    int32_t position_steps;
} bm_algo_stepper_state_t;

void bm_algo_stepper_reset(bm_algo_stepper_state_t *state, int32_t position);
uint32_t bm_algo_stepper_process(bm_algo_stepper_state_t *state,
                                 const bm_algo_stepper_config_t *config,
                                 float velocity_steps_s,
                                 float dt_s,
                                 int8_t *pulses,
                                 uint32_t max_pulses);

/* ---------- 编码器 index/丢脉冲诊断 ---------- */
#define BM_ALGO_ENCODER_FAULT_NONE    0u
#define BM_ALGO_ENCODER_FAULT_MISSED  (1u << 0u)
#define BM_ALGO_ENCODER_FAULT_INDEX   (1u << 1u)

typedef struct {
    int32_t max_delta_per_step;
} bm_algo_encoder_diag_config_t;

typedef struct {
    int32_t prev_count;
} bm_algo_encoder_diag_state_t;

void bm_algo_encoder_diag_reset(bm_algo_encoder_diag_state_t *state,
                                int32_t raw_count);

/**
 * @brief 编码器诊断单步：检测计数跳变与 index 事件
 *
 * @param state 诊断状态
 * @param config 阈值配置（max_delta_per_step）
 * @param raw_count 当前原始计数
 * @param index_pulse_seen 本周期是否检测到 index 脉冲（非 0 为真）
 * @return fault_flags（BM_ALGO_ENCODER_FAULT_* 位或）
 */
uint32_t bm_algo_encoder_diag_step(bm_algo_encoder_diag_state_t *state,
                                   const bm_algo_encoder_diag_config_t *config,
                                   int32_t raw_count,
                                   int index_pulse_seen);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_MOTION_H */

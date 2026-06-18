/**
 * @file bm_algo_control.h
 * @brief 控制算法：积分器、微分器、PI/PID、PR 与补偿器
 *
 * state/config 显式分离，step 接受显式 dt_s，适用于 HRT 与 SRT。
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
#ifndef BM_ALGO_CONTROL_H
#define BM_ALGO_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 积分器 ---------- */
typedef struct {
    float min;
    float max;
} bm_algo_integrator_config_t;

typedef struct {
    float integrator;
} bm_algo_integrator_state_t;

void bm_algo_integrator_reset(bm_algo_integrator_state_t *state, float value);
float bm_algo_integrator_step(bm_algo_integrator_state_t *state,
                              const bm_algo_integrator_config_t *config,
                              float input,
                              float dt_s);

/* ---------- 微分器（一阶低通微分） ---------- */
typedef struct {
    float coeff;       /**< 微分低通系数，越大带宽越宽 */
} bm_algo_differentiator_config_t;

typedef struct {
    float prev_input;
    float derivative;
} bm_algo_differentiator_state_t;

void bm_algo_differentiator_reset(bm_algo_differentiator_state_t *state);
float bm_algo_differentiator_step(bm_algo_differentiator_state_t *state,
                                  const bm_algo_differentiator_config_t *config,
                                  float input,
                                  float dt_s);

/* ---------- PI ---------- */
typedef struct {
    float kp;
    float ki;
    float out_min;
    float out_max;
    float integrator_min;
    float integrator_max;
} bm_algo_pi_config_t;

typedef struct {
    float integrator;
    float output;
} bm_algo_pi_state_t;

int bm_algo_pi_validate_config(const bm_algo_pi_config_t *config);
void bm_algo_pi_reset(bm_algo_pi_state_t *state, float output);
float bm_algo_pi_step(bm_algo_pi_state_t *state,
                      const bm_algo_pi_config_t *config,
                      float error,
                      float dt_s);
void bm_algo_pi_bumpless_reset(bm_algo_pi_state_t *state,
                               const bm_algo_pi_config_t *config,
                               float output);

/* ---------- PID ---------- */
typedef struct {
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
    float integrator_min;
    float integrator_max;
    float d_filter_coeff;  /**< 微分项一阶低通，0=无滤波 */
} bm_algo_pid_config_t;

typedef struct {
    float integrator;
    float prev_error;
    float d_filtered;
    float output;
} bm_algo_pid_state_t;

int bm_algo_pid_validate_config(const bm_algo_pid_config_t *config);
void bm_algo_pid_reset(bm_algo_pid_state_t *state, float output);
float bm_algo_pid_step(bm_algo_pid_state_t *state,
                       const bm_algo_pid_config_t *config,
                       float error,
                       float dt_s);

/* ---------- PR（谐振控制器，离散化双线性） ---------- */
typedef struct {
    float kp;
    float kr;
    float omega_rad_s;   /**< 谐振角频率 */
    float bandwidth_rad_s; /**< 谐振带宽 */
    float out_min;
    float out_max;
} bm_algo_pr_config_t;

typedef struct {
    float x1;
    float x2;
    float y1;
    float y2;
    float output;
} bm_algo_pr_state_t;

int bm_algo_pr_init(bm_algo_pr_state_t *state,
                    const bm_algo_pr_config_t *config,
                    float sample_period_s);
void bm_algo_pr_reset(bm_algo_pr_state_t *state);
float bm_algo_pr_step(bm_algo_pr_state_t *state,
                      const bm_algo_pr_config_t *config,
                      float error,
                      float b0, float b1, float b2,
                      float a1, float a2);

/* PR 系数（由 init 写入调用方缓冲） */
int bm_algo_pr_compute_coeffs(const bm_algo_pr_config_t *config,
                              float sample_period_s,
                              float *b0, float *b1, float *b2,
                              float *a1, float *a2);

/* ---------- 超前滞后 ---------- */
typedef struct {
    float zero_rad_s;
    float pole_rad_s;
    float gain;
} bm_algo_lead_lag_config_t;

typedef struct {
    float x1;
    float y1;
    float b0, b1, a1;
} bm_algo_lead_lag_state_t;

int bm_algo_lead_lag_init(bm_algo_lead_lag_state_t *state,
                          const bm_algo_lead_lag_config_t *config,
                          float sample_period_s);
void bm_algo_lead_lag_reset(bm_algo_lead_lag_state_t *state);
float bm_algo_lead_lag_step(bm_algo_lead_lag_state_t *state, float input);

/* ---------- 前馈 ---------- */
float bm_algo_feedforward_step(float reference, float gain, float bias);

/* ---------- 2DOF PID（设定值加权） ---------- */
typedef struct {
    float kp;
    float ki;
    float kd;
    float b;  /**< P/D 项设定值权重 */
    float out_min;
    float out_max;
    float integrator_min;
    float integrator_max;
    float d_filter_coeff;
} bm_algo_pid2_config_t;

typedef struct {
    float integrator;
    float prev_measurement;
    float d_filtered;
    float output;
} bm_algo_pid2_state_t;

void bm_algo_pid2_reset(bm_algo_pid2_state_t *state, float output);
float bm_algo_pid2_step(bm_algo_pid2_state_t *state,
                        const bm_algo_pid2_config_t *config,
                        float reference,
                        float measurement,
                        float dt_s);

/* ---------- Smith 预估器（死区补偿接口） ---------- */
typedef struct {
    float model_gain;
    uint32_t delay_steps;
} bm_algo_smith_predictor_config_t;

typedef struct {
    float *u_delay_line;
    uint32_t line_len;
    uint32_t delay_steps;
    uint32_t head;
    float y_model;
    float y_delayed;
} bm_algo_smith_predictor_state_t;

int bm_algo_smith_predictor_init(bm_algo_smith_predictor_state_t *state,
                                 const bm_algo_smith_predictor_config_t *config,
                                 float *delay_line,
                                 uint32_t line_len);
void bm_algo_smith_predictor_reset(bm_algo_smith_predictor_state_t *state,
                                   const bm_algo_smith_predictor_config_t *config);
/** 返回 reference 与 Smith 预测过程输出之间的控制误差。 */
float bm_algo_smith_predictor_step(bm_algo_smith_predictor_state_t *state,
                                   const bm_algo_smith_predictor_config_t *config,
                                   float reference,
                                   float measurement,
                                   float u_controller);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_CONTROL_H */

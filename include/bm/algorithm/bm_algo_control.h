/**
 * @file bm_algo_control.h
 * @brief 控制算法：积分器、微分器、PI/PID、PR 与补偿器
 *
 * state/config 显式分离，step 接受显式 dt_s，适用于 HRT 与 SRT。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.2            zeh            bm_algo_pr_init 补 Doxygen 设计契约，说明调用方须自行获取并传递 PR 系数
 * 2026-07-09       1.3            zeh            疑似-2/8：bm_algo_lead_lag_init
 *                                                返回值补 k+p==0 语义说明；
 *                                                新增 bm_algo_pid2_validate_config，
 *                                                与 pi/pid 家族的校验函数对齐
 * 2026-07-27       1.4            zeh            pi/pid/pid2 validate_config
 *                                                返回值统一为 BM_OK /
 *                                                BM_ERR_INVALID
 * 2026-07-28       1.5            zeh            状态返回文档对齐 BM_OK/BM_ERR_*
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_CONTROL_H
#define BM_ALGO_CONTROL_H

#include "bm/algorithm/bm_algo_errors.h"

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

/**
 * @brief 校验 PI 控制器配置
 *
 * @param config PI 配置指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
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

/**
 * @brief 校验 PID 控制器配置
 *
 * @param config PID 配置指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
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

/**
 * @brief PR 控制器初始化：复位状态并校验配置合法性
 *
 * @note 设计契约：本函数 **仅** 做状态复位与配置合法性校验，不输出系数。
 *       调用方须在每次 step 前自行调用 bm_algo_pr_compute_coeffs() 获取
 *       b0/b1/b2/a1/a2，并将其显式传入 bm_algo_pr_step()。
 *
 * @param state           PR 状态（不可为 NULL）
 * @param config          PR 配置（不可为 NULL）
 * @param sample_period_s 采样周期（s，>0）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或系数计算失败（配置不合法）
 */
int bm_algo_pr_init(bm_algo_pr_state_t *state,
                    const bm_algo_pr_config_t *config,
                    float sample_period_s);
void bm_algo_pr_reset(bm_algo_pr_state_t *state);
float bm_algo_pr_step(bm_algo_pr_state_t *state,
                      const bm_algo_pr_config_t *config,
                      float error,
                      float b0, float b1, float b2,
                      float a1, float a2);

/**
 * @brief 计算 PR 控制器离散系数（双线性变换）
 *
 * @note 调用方须将返回的 b0/b1/b2/a1/a2 保存，并在每次 bm_algo_pr_step 时传入。
 *
 * @param config          PR 配置（不可为 NULL）
 * @param sample_period_s 采样周期（s，>0）
 * @param b0,b1,b2        分子系数输出指针（均不可为 NULL）
 * @param a1,a2           分母系数输出指针（均不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
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

/**
 * @brief 校验 2DOF PID 配置参数（疑似-8：与 bm_algo_pi_validate_config/
 *        bm_algo_pid_validate_config 对齐，此前 pid2 家族缺该校验函数，
 *        out_min>out_max 或 integrator_min>integrator_max 时上层无法
 *        提前拒绝，只能在 bm_algo_clamp_f 内静默产出意义不明的钳位结果）
 *
 * @param config 待校验配置指针
 * @return BM_OK 合法；BM_ERR_INVALID 参数无效
 */
int bm_algo_pid2_validate_config(const bm_algo_pid2_config_t *config);
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

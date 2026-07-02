/**
 * @file bm_algo_control.c
 * @brief 控制算法：积分器、微分器、PI/PID、PR 与补偿器实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-13       1.1            zeh            增加 Smith 预估器
 * 2026-06-23       1.2            zeh            bm_algo_pr_init 补 Doxygen 设计契约注释，清理 (void) 死代码，变量改名使意图清晰
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_errors.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

void bm_algo_integrator_reset(bm_algo_integrator_state_t *state, float value) {
    if (state != NULL) {
        state->integrator = value;
    }
}

float bm_algo_integrator_step(bm_algo_integrator_state_t *state,
                              const bm_algo_integrator_config_t *config,
                              float input,
                              float dt_s) {
    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return input;
    }

    state->integrator += input * dt_s;
    state->integrator = bm_algo_clamp_f(state->integrator,
                                          config->min,
                                          config->max);
    return state->integrator;
}

void bm_algo_differentiator_reset(bm_algo_differentiator_state_t *state) {
    if (state != NULL) {
        state->prev_input = 0.0f;
        state->derivative = 0.0f;
    }
}

float bm_algo_differentiator_step(bm_algo_differentiator_state_t *state,
                                  const bm_algo_differentiator_config_t *config,
                                  float input,
                                  float dt_s) {
    float raw_d;
    float alpha;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return 0.0f;
    }

    raw_d = (input - state->prev_input) / dt_s;
    state->prev_input = input;

    alpha = bm_algo_clamp_f(config->coeff, 0.0f, 1.0f);
    state->derivative += alpha * (raw_d - state->derivative);
    return state->derivative;
}

int bm_algo_pi_validate_config(const bm_algo_pi_config_t *config) {
    if (config == NULL) {
        return BM_ALGO_ERR_INVALID;
    }
    if (config->out_min > config->out_max) {
        return BM_ALGO_ERR_INVALID;
    }
    if (config->integrator_min > config->integrator_max) {
        return BM_ALGO_ERR_INVALID;
    }
    return 0;
}

void bm_algo_pi_reset(bm_algo_pi_state_t *state, float output) {
    if (state != NULL) {
        state->integrator = 0.0f;
        state->output = output;
    }
}

void bm_algo_pi_bumpless_reset(bm_algo_pi_state_t *state,
                               const bm_algo_pi_config_t *config,
                               float output) {
    if (state == NULL || config == NULL) {
        return;
    }

    output = bm_algo_clamp_f(output, config->out_min, config->out_max);

    if (config->ki != 0.0f) {
        state->integrator = output / config->ki;
        state->integrator = bm_algo_clamp_f(state->integrator,
                                            config->integrator_min,
                                            config->integrator_max);
    } else {
        state->integrator = 0.0f;
    }
    state->output = output;
}

float bm_algo_pi_step(bm_algo_pi_state_t *state,
                      const bm_algo_pi_config_t *config,
                      float error,
                      float dt_s) {
    float p_term;
    float u_unsat;
    float u_sat;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return 0.0f;
    }

    if (!bm_algo_is_finite_f(error)) {
        return state->output;
    }

    p_term = config->kp * error;

    state->integrator += error * dt_s;
    state->integrator = bm_algo_clamp_f(state->integrator,
                                        config->integrator_min,
                                        config->integrator_max);

    u_unsat = p_term + config->ki * state->integrator;
    u_sat = bm_algo_clamp_f(u_unsat, config->out_min, config->out_max);

    /* 反算抗积分饱和 */
    if (config->ki != 0.0f && u_sat != u_unsat) {
        state->integrator = (u_sat - p_term) / config->ki;
        state->integrator = bm_algo_clamp_f(state->integrator,
                                            config->integrator_min,
                                            config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

int bm_algo_pid_validate_config(const bm_algo_pid_config_t *config) {
    if (config == NULL) {
        return BM_ALGO_ERR_INVALID;
    }
    if (config->out_min > config->out_max) {
        return BM_ALGO_ERR_INVALID;
    }
    return 0;
}

void bm_algo_pid_reset(bm_algo_pid_state_t *state, float output) {
    if (state != NULL) {
        state->integrator = 0.0f;
        state->prev_error = 0.0f;
        state->d_filtered = 0.0f;
        state->output = output;
    }
}

float bm_algo_pid_step(bm_algo_pid_state_t *state,
                       const bm_algo_pid_config_t *config,
                       float error,
                       float dt_s) {
    float p_term;
    float d_raw;
    float d_term;
    float u_unsat;
    float u_sat;
    float alpha;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return 0.0f;
    }

    if (!bm_algo_is_finite_f(error)) {
        return state->output;
    }

    p_term = config->kp * error;

    state->integrator += error * dt_s;
    state->integrator = bm_algo_clamp_f(state->integrator,
                                        config->integrator_min,
                                        config->integrator_max);

    d_raw = (error - state->prev_error) / dt_s;
    state->prev_error = error;

    alpha = bm_algo_clamp_f(config->d_filter_coeff, 0.0f, 1.0f);
    state->d_filtered += alpha * (d_raw - state->d_filtered);
    d_term = config->kd * state->d_filtered;

    u_unsat = p_term + config->ki * state->integrator + d_term;
    u_sat = bm_algo_clamp_f(u_unsat, config->out_min, config->out_max);

    if (config->ki != 0.0f && u_sat != u_unsat) {
        state->integrator = (u_sat - p_term - d_term) / config->ki;
        state->integrator = bm_algo_clamp_f(state->integrator,
                                            config->integrator_min,
                                            config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

int bm_algo_pr_compute_coeffs(const bm_algo_pr_config_t *config,
                              float sample_period_s,
                              float *b0, float *b1, float *b2,
                              float *a1, float *a2) {
    float w0;
    float wc;
    float k;
    float d;
    float a0;

    if (config == NULL || sample_period_s <= 0.0f ||
        b0 == NULL || b1 == NULL || b2 == NULL ||
        a1 == NULL || a2 == NULL) {
        return BM_ALGO_ERR_INVALID;
    }

    w0 = config->omega_rad_s;
    wc = config->bandwidth_rad_s;
    k = 2.0f / sample_period_s;

  /* 双线性变换离散谐振器（带阻尼） */
    d = k * k + w0 * w0 + 2.0f * wc * k;
    if (d == 0.0f) {
        return BM_ALGO_ERR_INVALID;
    }

    a0 = d;
    *b0 = (config->kr * wc * k) / a0;
    *b1 = 0.0f;
    *b2 = -(*b0);
    *a1 = (2.0f * w0 * w0 - 2.0f * k * k) / a0;
    *a2 = (k * k - 2.0f * wc * k + w0 * w0) / a0;
    return 0;
}

/**
 * @brief PR 控制器初始化：复位状态并校验配置
 *
 * @note 设计契约：本函数 **仅** 做状态复位与配置合法性校验，不向调用方输出系数。
 *       调用方须在每次 step 前自行调用 bm_algo_pr_compute_coeffs() 获取
 *       b0/b1/b2/a1/a2，并将其显式传入 bm_algo_pr_step()。
 *       如果参数校验失败（config->omega_rad_s、bandwidth_rad_s 等导致分母为零），
 *       本函数返回 -1，调用方不应继续调用 step。
 *
 * @param state          PR 状态（不可为 NULL）
 * @param config         PR 配置（不可为 NULL）
 * @param sample_period_s 采样周期（s，>0）
 * @return 0 成功；-1 参数无效或系数计算失败
 */
int bm_algo_pr_init(bm_algo_pr_state_t *state,
                    const bm_algo_pr_config_t *config,
                    float sample_period_s) {
    /* 临时接收 compute_coeffs 输出，用于校验配置合法性；结果由调用方自行保存 */
    float b0_check, b1_check, b2_check, a1_check, a2_check;

    if (state == NULL || config == NULL) {
        return BM_ALGO_ERR_INVALID;
    }
    bm_algo_pr_reset(state);
    /* 调用 compute_coeffs 做配置校验（e.g. 分母不为零）；
     * 调用方须自行再次调用 bm_algo_pr_compute_coeffs 并将系数传入 bm_algo_pr_step */
    return bm_algo_pr_compute_coeffs(config, sample_period_s,
                                     &b0_check, &b1_check, &b2_check,
                                     &a1_check, &a2_check);
}

void bm_algo_pr_reset(bm_algo_pr_state_t *state) {
    if (state != NULL) {
        state->x1 = 0.0f;
        state->x2 = 0.0f;
        state->y1 = 0.0f;
        state->y2 = 0.0f;
        state->output = 0.0f;
    }
}

float bm_algo_pr_step(bm_algo_pr_state_t *state,
                      const bm_algo_pr_config_t *config,
                      float error,
                      float b0, float b1, float b2,
                      float a1, float a2) {
    float y;

    if (state == NULL || config == NULL) {
        return 0.0f;
    }

    y = b0 * error + b1 * state->x1 + b2 * state->x2
        - a1 * state->y1 - a2 * state->y2;

    state->x2 = state->x1;
    state->x1 = error;
    state->y2 = state->y1;
    state->y1 = y;

    state->output = config->kp * error + y;
    state->output = bm_algo_clamp_f(state->output,
                                    config->out_min,
                                    config->out_max);
    return state->output;
}

int bm_algo_lead_lag_init(bm_algo_lead_lag_state_t *state,
                          const bm_algo_lead_lag_config_t *config,
                          float sample_period_s) {
    float k;
    float z;
    float p;

    if (state == NULL || config == NULL || sample_period_s <= 0.0f) {
        return BM_ALGO_ERR_INVALID;
    }

    k = 2.0f / sample_period_s;
    z = config->zero_rad_s;
    p = config->pole_rad_s;

    state->b0 = config->gain * (k + z) / (k + p);
    state->b1 = config->gain * (z - k) / (k + p);
    state->a1 = (p - k) / (k + p);
    bm_algo_lead_lag_reset(state);
    return 0;
}

void bm_algo_lead_lag_reset(bm_algo_lead_lag_state_t *state) {
    if (state != NULL) {
        state->x1 = 0.0f;
        state->y1 = 0.0f;
    }
}

float bm_algo_lead_lag_step(bm_algo_lead_lag_state_t *state, float input) {
    float y;

    if (state == NULL) {
        return input;
    }

    y = state->b0 * input + state->b1 * state->x1 - state->a1 * state->y1;
    state->x1 = input;
    state->y1 = y;
    return y;
}

float bm_algo_feedforward_step(float reference, float gain, float bias) {
    return reference * gain + bias;
}

void bm_algo_pid2_reset(bm_algo_pid2_state_t *state, float output) {
    if (state != NULL) {
        state->integrator = 0.0f;
        state->prev_measurement = 0.0f;
        state->d_filtered = 0.0f;
        state->output = output;
    }
}

float bm_algo_pid2_step(bm_algo_pid2_state_t *state,
                        const bm_algo_pid2_config_t *config,
                        float reference,
                        float measurement,
                        float dt_s) {
    float error_i;
    float p_term;
    float d_raw;
    float d_term;
    float u_unsat;
    float u_sat;
    float alpha;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return 0.0f;
    }

    error_i = reference - measurement;
    p_term = config->kp * (config->b * reference - measurement);

    state->integrator += error_i * dt_s;
    state->integrator = bm_algo_clamp_f(state->integrator,
                                        config->integrator_min,
                                        config->integrator_max);

    d_raw = -(measurement - state->prev_measurement) / dt_s;
    state->prev_measurement = measurement;
    alpha = bm_algo_clamp_f(config->d_filter_coeff, 0.0f, 1.0f);
    state->d_filtered += alpha * (d_raw - state->d_filtered);
    d_term = config->kd * state->d_filtered;

    u_unsat = p_term + config->ki * state->integrator + d_term;
    u_sat = bm_algo_clamp_f(u_unsat, config->out_min, config->out_max);

    if (config->ki != 0.0f && u_sat != u_unsat) {
        state->integrator = (u_sat - p_term - d_term) / config->ki;
        state->integrator = bm_algo_clamp_f(state->integrator,
                                            config->integrator_min,
                                            config->integrator_max);
    }

    state->output = u_sat;
    return state->output;
}

int bm_algo_smith_predictor_init(bm_algo_smith_predictor_state_t *state,
                                 const bm_algo_smith_predictor_config_t *config,
                                 float *delay_line,
                                 uint32_t line_len) {
    if (state == NULL || config == NULL || delay_line == NULL ||
        config->delay_steps == 0u || line_len < config->delay_steps) {
        return BM_ALGO_ERR_INVALID;
    }

    state->u_delay_line = delay_line;
    state->line_len = line_len;
    state->delay_steps = config->delay_steps;
    bm_algo_smith_predictor_reset(state, config);
    return 0;
}

void bm_algo_smith_predictor_reset(bm_algo_smith_predictor_state_t *state,
                                   const bm_algo_smith_predictor_config_t *config) {
    uint32_t i;

    if (state == NULL || state->u_delay_line == NULL || config == NULL) {
        return;
    }
    if (config->delay_steps == 0u ||
        config->delay_steps > state->line_len ||
        config->delay_steps != state->delay_steps) {
        return;
    }

    for (i = 0u; i < config->delay_steps; ++i) {
        state->u_delay_line[i] = 0.0f;
    }
    state->head = 0u;
    state->y_model = 0.0f;
    state->y_delayed = 0.0f;
}

float bm_algo_smith_predictor_step(bm_algo_smith_predictor_state_t *state,
                                   const bm_algo_smith_predictor_config_t *config,
                                   float reference,
                                   float measurement,
                                   float u_controller) {
    float u_delayed;
    float y_nd;
    float y_predicted;

    if (state == NULL || config == NULL || state->u_delay_line == NULL ||
        config->delay_steps == 0u ||
        config->delay_steps > state->line_len ||
        config->delay_steps != state->delay_steps ||
        state->head >= config->delay_steps) {
        return reference - measurement;
    }

    u_delayed = state->u_delay_line[state->head];
    y_nd = config->model_gain * u_controller;
    state->y_delayed = config->model_gain * u_delayed;
    y_predicted = y_nd + measurement - state->y_delayed;

    state->u_delay_line[state->head] = u_controller;
    state->head = (state->head + 1u) % config->delay_steps;
    state->y_model = y_nd;

    return reference - y_predicted;
}

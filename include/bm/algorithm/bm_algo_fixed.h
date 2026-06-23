/**
 * @file bm_algo_fixed.h
 * @brief 定点算法核：Q31/Q15 显式后缀 API（与 float 并存）
 *
 * Q31：1.0 ≈ 0x7FFFFFFF；Q15：1.0 = 32767。系数与信号均按 ±1.0 归一化。
 * 与 float 核分文件、分符号，不使用全局 typedef 在编译期切换 ABI。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 2.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            Q31 斜坡与 S 曲线
 * 2026-06-17       1.2            zeh            Q15 滑动平均/PID 与 Q31 迟滞
 * 2026-06-17       1.3            zeh            Q31 梯形速度曲线
 * 2026-06-17       1.4            zeh            Q31 LPF、Q15 中值/高通
 * 2026-06-17       1.5            zeh            Q31 微分器、Q15 包络/RMS
 * 2026-06-17       1.6            zeh            定点第七批：库仑/高通/滑动平均 Q31、死区/滞回 Q15
 * 2026-06-17       1.7            zeh            定点第八批：积分/限速/超前滞后 Q15、二阶 IIR Q31
 * 2026-06-17       1.8            zeh            定点第九批：DOB Q15、包络/RMS Q31、背隙逆补偿 Q31
 * 2026-06-17       1.9            zeh            定点第十批：微分/DOB/库仑/超前滞后/互补/前馈 Q15/Q31
 * 2026-06-17       2.0            zeh            定点第十一批：PI/PR/斜坡/梯形/冗余/速率/SOC 融合 Q15/Q31
 * 2026-06-17       2.1            zeh            定点第十二批：S 曲线/MPPT/信号质量/Wh 积分 Q15/Q31
 * 2026-06-17       2.2            zeh            定点第十四批：全族 Q31/Q15 后缀 API 收口
 * 2026-06-23       2.3            zeh            缺陷修复：Mahony Q15/Q31 state 新增 Ki 积分持久化字段
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGO_FIXED_H
#define BM_ALGO_FIXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t bm_algo_q31_t;
typedef int16_t bm_algo_q15_t;

#define BM_ALGO_Q31_ONE   ((bm_algo_q31_t)2147483647)
#define BM_ALGO_Q15_ONE   ((bm_algo_q15_t)32767)

bm_algo_q31_t bm_algo_clamp_q31(bm_algo_q31_t value,
                                bm_algo_q31_t min_v,
                                bm_algo_q31_t max_v);

bm_algo_q15_t bm_algo_clamp_q15(bm_algo_q15_t value,
                                bm_algo_q15_t min_v,
                                bm_algo_q15_t max_v);

/** float → Q31（饱和） */
bm_algo_q31_t bm_algo_float_to_q31(float value);

/** Q31 → float */
float bm_algo_q31_to_float(bm_algo_q31_t value);

/** float → Q15（饱和） */
bm_algo_q15_t bm_algo_float_to_q15(float value);

/** Q15 → float */
float bm_algo_q15_to_float(bm_algo_q15_t value);

typedef struct {
    bm_algo_q31_t kp;
    bm_algo_q31_t ki;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
    bm_algo_q31_t integrator_min;
    bm_algo_q31_t integrator_max;
} bm_algo_pi_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
    bm_algo_q31_t output;
} bm_algo_pi_q31_state_t;

void bm_algo_pi_q31_reset(bm_algo_pi_q31_state_t *state, bm_algo_q31_t output);

bm_algo_q31_t bm_algo_pi_q31_step(bm_algo_pi_q31_state_t *state,
                                  const bm_algo_pi_q31_config_t *config,
                                  bm_algo_q31_t error,
                                  bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_lpf1_q15_config_t;

typedef struct {
    bm_algo_q15_t output;
} bm_algo_lpf1_q15_state_t;

void bm_algo_lpf1_q15_reset(bm_algo_lpf1_q15_state_t *state, bm_algo_q15_t output);

bm_algo_q15_t bm_algo_lpf1_q15_step(bm_algo_lpf1_q15_state_t *state,
                                    const bm_algo_lpf1_q15_config_t *config,
                                    bm_algo_q15_t input);

typedef struct {
    bm_algo_q31_t kp;
    bm_algo_q31_t ki;
    bm_algo_q31_t kd;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
    bm_algo_q31_t integrator_min;
    bm_algo_q31_t integrator_max;
    bm_algo_q31_t d_filter_alpha_q31;
} bm_algo_pid_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
    bm_algo_q31_t prev_error;
    bm_algo_q31_t d_filtered;
    bm_algo_q31_t output;
} bm_algo_pid_q31_state_t;

void bm_algo_pid_q31_reset(bm_algo_pid_q31_state_t *state, bm_algo_q31_t output);

bm_algo_q31_t bm_algo_pid_q31_step(bm_algo_pid_q31_state_t *state,
                                   const bm_algo_pid_q31_config_t *config,
                                   bm_algo_q31_t error,
                                   bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t b0;
    bm_algo_q15_t b1;
    bm_algo_q15_t b2;
    bm_algo_q15_t a1;
    bm_algo_q15_t a2;
} bm_algo_biquad_q15_config_t;

typedef struct {
    bm_algo_q15_t z1;
    bm_algo_q15_t z2;
} bm_algo_biquad_q15_state_t;

void bm_algo_biquad_q15_reset(bm_algo_biquad_q15_state_t *state);

bm_algo_q15_t bm_algo_biquad_q15_step(bm_algo_biquad_q15_state_t *state,
                                      const bm_algo_biquad_q15_config_t *config,
                                      bm_algo_q15_t input);

/* ---------- 电机控制 Q31 算法族 ---------- */
typedef struct {
    bm_algo_q31_t ia;
    bm_algo_q31_t ib;
    bm_algo_q31_t ic;
} bm_algo_abc_q31_t;

typedef struct {
    bm_algo_q31_t i_alpha;
    bm_algo_q31_t i_beta;
} bm_algo_alphabeta_q31_t;

typedef struct {
    bm_algo_q31_t id;
    bm_algo_q31_t iq;
} bm_algo_dq_q31_t;

typedef struct {
    bm_algo_q31_t duty_a; /* 0 ~ BM_ALGO_Q31_ONE */
    bm_algo_q31_t duty_b;
    bm_algo_q31_t duty_c;
} bm_algo_svpwm_q31_out_t;

/** Clarke：三相 → αβ（幅值不变） */
void bm_algo_clarke_q31(const bm_algo_abc_q31_t *abc, bm_algo_alphabeta_q31_t *ab);

/** 两相电流 Clarke（假定 ia+ib+ic=0） */
void bm_algo_clarke_2shunt_q31(bm_algo_q31_t ia, bm_algo_q31_t ib, bm_algo_alphabeta_q31_t *ab);

/** Park：αβ → dq。sin_theta 和 cos_theta 需外部传入（基于查表） */
void bm_algo_park_q31(const bm_algo_alphabeta_q31_t *ab,
                      bm_algo_q31_t sin_theta,
                      bm_algo_q31_t cos_theta,
                      bm_algo_dq_q31_t *dq);

/** 逆 Park：dq → αβ */
void bm_algo_inv_park_q31(const bm_algo_dq_q31_t *dq,
                          bm_algo_q31_t sin_theta,
                          bm_algo_q31_t cos_theta,
                          bm_algo_alphabeta_q31_t *ab);

/** SVPWM Q31 实现 */
void bm_algo_svpwm_q31(bm_algo_q31_t v_alpha,
                       bm_algo_q31_t v_beta,
                       bm_algo_svpwm_q31_out_t *out);

/* ---------- 通用定点扩展（第一批） ---------- */
typedef struct {
    bm_algo_q31_t min;
    bm_algo_q31_t max;
} bm_algo_integrator_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
} bm_algo_integrator_q31_state_t;

void bm_algo_integrator_q31_reset(bm_algo_integrator_q31_state_t *state,
                                  bm_algo_q31_t value);

bm_algo_q31_t bm_algo_integrator_q31_step(bm_algo_integrator_q31_state_t *state,
                                          const bm_algo_integrator_q31_config_t *config,
                                          bm_algo_q31_t input,
                                          bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t max_rise_per_s_q31;
    bm_algo_q31_t max_fall_per_s_q31;
} bm_algo_rate_limit_q31_config_t;

typedef struct {
    bm_algo_q31_t output;
} bm_algo_rate_limit_q31_state_t;

void bm_algo_rate_limit_q31_reset(bm_algo_rate_limit_q31_state_t *state,
                                  bm_algo_q31_t output);

bm_algo_q31_t bm_algo_rate_limit_q31_step(bm_algo_rate_limit_q31_state_t *state,
                                          const bm_algo_rate_limit_q31_config_t *config,
                                          bm_algo_q31_t target,
                                          bm_algo_q31_t dt_q31);

/** 死区：|value| < width_q31 时输出 0 */
bm_algo_q31_t bm_algo_deadband_q31(bm_algo_q31_t value, bm_algo_q31_t width_q31);

typedef struct {
    bm_algo_q15_t b0;
    bm_algo_q15_t b1;
    bm_algo_q15_t b2;
    bm_algo_q15_t a1;
    bm_algo_q15_t a2;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
} bm_algo_pr_q15_config_t;

typedef struct {
    bm_algo_q15_t x1;
    bm_algo_q15_t x2;
    bm_algo_q15_t y1;
    bm_algo_q15_t y2;
    bm_algo_q15_t output;
} bm_algo_pr_q15_state_t;

void bm_algo_pr_q15_reset(bm_algo_pr_q15_state_t *state);

bm_algo_q15_t bm_algo_pr_q15_step(bm_algo_pr_q15_state_t *state,
                                  const bm_algo_pr_q15_config_t *config,
                                  bm_algo_q15_t error);

/* ---------- 通用定点扩展（第二批：轨迹） ---------- */
typedef struct {
    bm_algo_q31_t rate_per_s_q31;
} bm_algo_ramp_q31_config_t;

typedef struct {
    bm_algo_q31_t output;
    int done;
} bm_algo_ramp_q31_state_t;

void bm_algo_ramp_q31_reset(bm_algo_ramp_q31_state_t *state, bm_algo_q31_t output);

bm_algo_q31_t bm_algo_ramp_q31_step(bm_algo_ramp_q31_state_t *state,
                                    const bm_algo_ramp_q31_config_t *config,
                                    bm_algo_q31_t target,
                                    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t max_vel_q31;
    bm_algo_q31_t max_accel_q31;
    bm_algo_q31_t max_jerk_q31;
} bm_algo_scurve_q31_config_t;

typedef struct {
    bm_algo_q31_t position;
    bm_algo_q31_t velocity;
    bm_algo_q31_t acceleration;
    bm_algo_q31_t target;
    int done;
} bm_algo_scurve_q31_state_t;

void bm_algo_scurve_q31_reset(bm_algo_scurve_q31_state_t *state,
                              bm_algo_q31_t position,
                              bm_algo_q31_t velocity,
                              bm_algo_q31_t acceleration);

void bm_algo_scurve_q31_set_target(bm_algo_scurve_q31_state_t *state,
                                   bm_algo_q31_t target);

bm_algo_q31_t bm_algo_scurve_q31_step(bm_algo_scurve_q31_state_t *state,
                                      const bm_algo_scurve_q31_config_t *config,
                                      bm_algo_q31_t dt_q31);

/* ---------- 通用定点扩展（第三批） ---------- */
#define BM_ALGO_MOVING_AVG_Q15_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_moving_avg_q15_config_t;

typedef struct {
    bm_algo_q15_t samples[BM_ALGO_MOVING_AVG_Q15_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_moving_avg_q15_state_t;

void bm_algo_moving_avg_q15_reset(bm_algo_moving_avg_q15_state_t *state);

bm_algo_q15_t bm_algo_moving_avg_q15_step(bm_algo_moving_avg_q15_state_t *state,
                                          const bm_algo_moving_avg_q15_config_t *config,
                                          bm_algo_q15_t input);

typedef struct {
    bm_algo_q15_t kp;
    bm_algo_q15_t ki;
    bm_algo_q15_t kd;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
    bm_algo_q15_t integrator_min;
    bm_algo_q15_t integrator_max;
    bm_algo_q15_t d_filter_alpha_q15;
} bm_algo_pid_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
    bm_algo_q15_t prev_error;
    bm_algo_q15_t d_filtered;
    bm_algo_q15_t output;
} bm_algo_pid_q15_state_t;

void bm_algo_pid_q15_reset(bm_algo_pid_q15_state_t *state, bm_algo_q15_t output);

bm_algo_q15_t bm_algo_pid_q15_step(bm_algo_pid_q15_state_t *state,
                                   const bm_algo_pid_q15_config_t *config,
                                   bm_algo_q15_t error,
                                   bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t low_threshold;
    bm_algo_q31_t high_threshold;
} bm_algo_hysteresis_q31_config_t;

typedef struct {
    int output_on;
} bm_algo_hysteresis_q31_state_t;

void bm_algo_hysteresis_q31_reset(bm_algo_hysteresis_q31_state_t *state);

/** 迟滞比较：返回 BM_ALGO_Q31_ONE 或 0 */
bm_algo_q31_t bm_algo_hysteresis_q31_step(bm_algo_hysteresis_q31_state_t *state,
                                          const bm_algo_hysteresis_q31_config_t *config,
                                          bm_algo_q31_t input);

/* ---------- 通用定点扩展（第四批：轨迹） ---------- */
typedef struct {
    bm_algo_q31_t max_vel_q31;
    bm_algo_q31_t max_accel_q31;
    bm_algo_q31_t max_decel_q31;
} bm_algo_trapezoid_q31_config_t;

typedef struct {
    bm_algo_q31_t position;
    bm_algo_q31_t velocity;
    bm_algo_q31_t target;
    int done;
} bm_algo_trapezoid_q31_state_t;

void bm_algo_trapezoid_q31_reset(bm_algo_trapezoid_q31_state_t *state,
                                 bm_algo_q31_t position,
                                 bm_algo_q31_t velocity);

void bm_algo_trapezoid_q31_set_target(bm_algo_trapezoid_q31_state_t *state,
                                      bm_algo_q31_t target);

bm_algo_q31_t bm_algo_trapezoid_q31_step(bm_algo_trapezoid_q31_state_t *state,
                                         const bm_algo_trapezoid_q31_config_t *config,
                                         bm_algo_q31_t dt_q31);

/* ---------- 通用定点扩展（第五批：滤波） ---------- */
typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_lpf1_q31_config_t;

typedef struct {
    bm_algo_q31_t output;
} bm_algo_lpf1_q31_state_t;

void bm_algo_lpf1_q31_reset(bm_algo_lpf1_q31_state_t *state, bm_algo_q31_t output);

bm_algo_q31_t bm_algo_lpf1_q31_step(bm_algo_lpf1_q31_state_t *state,
                                    const bm_algo_lpf1_q31_config_t *config,
                                    bm_algo_q31_t input);

typedef struct {
    bm_algo_q15_t samples[3];
    uint8_t       count;
} bm_algo_median3_q15_state_t;

void bm_algo_median3_q15_reset(bm_algo_median3_q15_state_t *state);

bm_algo_q15_t bm_algo_median3_q15_step(bm_algo_median3_q15_state_t *state,
                                       bm_algo_q15_t input);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_hpf1_q15_config_t;

typedef struct {
    bm_algo_q15_t prev_input;
    bm_algo_q15_t prev_output;
} bm_algo_hpf1_q15_state_t;

void bm_algo_hpf1_q15_reset(bm_algo_hpf1_q15_state_t *state);

bm_algo_q15_t bm_algo_hpf1_q15_step(bm_algo_hpf1_q15_state_t *state,
                                    const bm_algo_hpf1_q15_config_t *config,
                                    bm_algo_q15_t input);

/* ---------- 通用定点扩展（第六批） ---------- */
typedef struct {
    bm_algo_q31_t coeff_q31;
} bm_algo_differentiator_q31_config_t;

typedef struct {
    bm_algo_q31_t prev_input;
    bm_algo_q31_t derivative;
} bm_algo_differentiator_q31_state_t;

void bm_algo_differentiator_q31_reset(bm_algo_differentiator_q31_state_t *state);

bm_algo_q31_t bm_algo_differentiator_q31_step(
    bm_algo_differentiator_q31_state_t *state,
    const bm_algo_differentiator_q31_config_t *config,
    bm_algo_q31_t input,
    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_envelope_q15_config_t;

typedef struct {
    bm_algo_q15_t envelope;
} bm_algo_envelope_q15_state_t;

void bm_algo_envelope_q15_reset(bm_algo_envelope_q15_state_t *state,
                                bm_algo_q15_t output);

bm_algo_q15_t bm_algo_envelope_q15_step(bm_algo_envelope_q15_state_t *state,
                                        const bm_algo_envelope_q15_config_t *config,
                                        bm_algo_q15_t input);

#define BM_ALGO_RMS_Q15_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_rms_q15_config_t;

typedef struct {
    bm_algo_q15_t samples[BM_ALGO_RMS_Q15_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_rms_q15_state_t;

void bm_algo_rms_q15_reset(bm_algo_rms_q15_state_t *state);

bm_algo_q15_t bm_algo_rms_q15_step(bm_algo_rms_q15_state_t *state,
                                   const bm_algo_rms_q15_config_t *config,
                                   bm_algo_q15_t input);

/* ---------- 通用定点扩展（第七批） ---------- */
typedef struct {
    bm_algo_q31_t nominal_capacity_q31;
    bm_algo_q31_t coulomb_efficiency_q31;
    bm_algo_q31_t soc_min;
    bm_algo_q31_t soc_max;
} bm_algo_coulomb_q31_config_t;

typedef struct {
    bm_algo_q31_t soc;
} bm_algo_coulomb_q31_state_t;

void bm_algo_coulomb_q31_reset(bm_algo_coulomb_q31_state_t *state,
                               bm_algo_q31_t soc_init);

/** 库仑 SOC 积分：电流与 dt 为 C-rate 与小时比例，容量为 Q31 满幅参考 */
bm_algo_q31_t bm_algo_coulomb_q31_step(bm_algo_coulomb_q31_state_t *state,
                                       const bm_algo_coulomb_q31_config_t *config,
                                       bm_algo_q31_t current_q31,
                                       bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_hpf1_q31_config_t;

typedef struct {
    bm_algo_q31_t prev_input;
    bm_algo_q31_t prev_output;
} bm_algo_hpf1_q31_state_t;

void bm_algo_hpf1_q31_reset(bm_algo_hpf1_q31_state_t *state);

bm_algo_q31_t bm_algo_hpf1_q31_step(bm_algo_hpf1_q31_state_t *state,
                                    const bm_algo_hpf1_q31_config_t *config,
                                    bm_algo_q31_t input);

#define BM_ALGO_MOVING_AVG_Q31_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_moving_avg_q31_config_t;

typedef struct {
    bm_algo_q31_t samples[BM_ALGO_MOVING_AVG_Q31_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_moving_avg_q31_state_t;

void bm_algo_moving_avg_q31_reset(bm_algo_moving_avg_q31_state_t *state);

bm_algo_q31_t bm_algo_moving_avg_q31_step(bm_algo_moving_avg_q31_state_t *state,
                                          const bm_algo_moving_avg_q31_config_t *config,
                                          bm_algo_q31_t input);

/** 死区：|value| < width_q15 时输出 0 */
bm_algo_q15_t bm_algo_deadband_q15(bm_algo_q15_t value, bm_algo_q15_t width_q15);

typedef struct {
    bm_algo_q15_t low_threshold;
    bm_algo_q15_t high_threshold;
} bm_algo_hysteresis_q15_config_t;

typedef struct {
    int output_on;
} bm_algo_hysteresis_q15_state_t;

void bm_algo_hysteresis_q15_reset(bm_algo_hysteresis_q15_state_t *state);

/** 迟滞比较：返回 BM_ALGO_Q15_ONE 或 0 */
bm_algo_q15_t bm_algo_hysteresis_q15_step(bm_algo_hysteresis_q15_state_t *state,
                                          const bm_algo_hysteresis_q15_config_t *config,
                                          bm_algo_q15_t input);

/* ---------- 通用定点扩展（第八批） ---------- */
typedef struct {
    bm_algo_q15_t min;
    bm_algo_q15_t max;
} bm_algo_integrator_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
} bm_algo_integrator_q15_state_t;

void bm_algo_integrator_q15_reset(bm_algo_integrator_q15_state_t *state,
                                  bm_algo_q15_t value);

bm_algo_q15_t bm_algo_integrator_q15_step(bm_algo_integrator_q15_state_t *state,
                                          const bm_algo_integrator_q15_config_t *config,
                                          bm_algo_q15_t input,
                                          bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t max_rise_per_s_q15;
    bm_algo_q15_t max_fall_per_s_q15;
} bm_algo_rate_limit_q15_config_t;

typedef struct {
    bm_algo_q15_t output;
} bm_algo_rate_limit_q15_state_t;

void bm_algo_rate_limit_q15_reset(bm_algo_rate_limit_q15_state_t *state,
                                  bm_algo_q15_t output);

bm_algo_q15_t bm_algo_rate_limit_q15_step(bm_algo_rate_limit_q15_state_t *state,
                                          const bm_algo_rate_limit_q15_config_t *config,
                                          bm_algo_q15_t target,
                                          bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t b0;
    bm_algo_q15_t b1;
    bm_algo_q15_t a1;
} bm_algo_lead_lag_q15_config_t;

typedef struct {
    bm_algo_q15_t x1;
    bm_algo_q15_t y1;
} bm_algo_lead_lag_q15_state_t;

/**
 * @brief 初始化超前滞后系数（从 config 拷贝至 state 内部缓冲）
 *
 * @param state 状态（不可为 NULL）
 * @param config 系数 b0/b1/a1（不可为 NULL）
 * @return 成功；参数无效
 */
int bm_algo_lead_lag_q15_init(bm_algo_lead_lag_q15_state_t *state,
                              const bm_algo_lead_lag_q15_config_t *config);

void bm_algo_lead_lag_q15_reset(bm_algo_lead_lag_q15_state_t *state);

bm_algo_q15_t bm_algo_lead_lag_q15_step(bm_algo_lead_lag_q15_state_t *state,
                                        const bm_algo_lead_lag_q15_config_t *config,
                                        bm_algo_q15_t input);

typedef struct {
    bm_algo_q31_t b0;
    bm_algo_q31_t b1;
    bm_algo_q31_t b2;
    bm_algo_q31_t a1;
    bm_algo_q31_t a2;
} bm_algo_biquad_q31_config_t;

typedef struct {
    bm_algo_q31_t z1;
    bm_algo_q31_t z2;
} bm_algo_biquad_q31_state_t;

void bm_algo_biquad_q31_reset(bm_algo_biquad_q31_state_t *state);

bm_algo_q31_t bm_algo_biquad_q31_step(bm_algo_biquad_q31_state_t *state,
                                      const bm_algo_biquad_q31_config_t *config,
                                      bm_algo_q31_t input);

/* ---------- 通用定点扩展（第九批） ---------- */
typedef struct {
    bm_algo_q15_t plant_gain_q15;
    bm_algo_q15_t lpf_alpha_q15;
} bm_algo_dob_q15_config_t;

typedef struct {
    bm_algo_q15_t y_hat;
    bm_algo_q15_t disturbance;
} bm_algo_dob_q15_state_t;

void bm_algo_dob_q15_reset(bm_algo_dob_q15_state_t *state);

/**
 * @brief DOB 单步（Q15）：由 u/y 估计扰动
 *
 * @param state 观测器状态（不可为 NULL）
 * @param config 模型增益与低通系数（不可为 NULL）
 * @param u_q15 控制输入
 * @param y_q15 被控输出测量
 * @param disturbance_out 可选扰动估计输出（可为 NULL）
 * @return 扰动估计（Q15）
 */
bm_algo_q15_t bm_algo_dob_q15_step(bm_algo_dob_q15_state_t *state,
                                   const bm_algo_dob_q15_config_t *config,
                                   bm_algo_q15_t u_q15,
                                   bm_algo_q15_t y_q15,
                                   bm_algo_q15_t *disturbance_out);

typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_envelope_q31_config_t;

typedef struct {
    bm_algo_q31_t envelope;
} bm_algo_envelope_q31_state_t;

void bm_algo_envelope_q31_reset(bm_algo_envelope_q31_state_t *state,
                                bm_algo_q31_t output);

bm_algo_q31_t bm_algo_envelope_q31_step(bm_algo_envelope_q31_state_t *state,
                                        const bm_algo_envelope_q31_config_t *config,
                                        bm_algo_q31_t input);

#define BM_ALGO_RMS_Q31_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_rms_q31_config_t;

typedef struct {
    bm_algo_q31_t samples[BM_ALGO_RMS_Q31_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_rms_q31_state_t;

void bm_algo_rms_q31_reset(bm_algo_rms_q31_state_t *state);

bm_algo_q31_t bm_algo_rms_q31_step(bm_algo_rms_q31_state_t *state,
                                   const bm_algo_rms_q31_config_t *config,
                                   bm_algo_q31_t input);

typedef struct {
    int last_direction;
    bm_algo_q31_t backlash_offset;
} bm_algo_backlash_q31_state_t;

void bm_algo_backlash_q31_reset(bm_algo_backlash_q31_state_t *state);

/**
 * @brief 背隙逆补偿（Q31）：换向时按 slope 渐补间隙
 *
 * @param command_q31 原始指令
 * @param state 背隙状态（不可为 NULL）
 * @param width_q31 总背隙宽度
 * @param slope_q31 每步最大补偿量（>0）
 * @return 补偿后指令
 */
bm_algo_q31_t bm_algo_backlash_inverse_q31(bm_algo_q31_t command_q31,
                                           bm_algo_backlash_q31_state_t *state,
                                           bm_algo_q31_t width_q31,
                                           bm_algo_q31_t slope_q31);

/* ---------- 通用定点扩展（第十批） ---------- */
typedef struct {
    bm_algo_q15_t coeff_q15;
} bm_algo_differentiator_q15_config_t;

typedef struct {
    bm_algo_q15_t prev_input;
    bm_algo_q15_t derivative;
} bm_algo_differentiator_q15_state_t;

void bm_algo_differentiator_q15_reset(bm_algo_differentiator_q15_state_t *state);

bm_algo_q15_t bm_algo_differentiator_q15_step(
    bm_algo_differentiator_q15_state_t *state,
    const bm_algo_differentiator_q15_config_t *config,
    bm_algo_q15_t input,
    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t plant_gain_q31;
    bm_algo_q31_t lpf_alpha_q31;
} bm_algo_dob_q31_config_t;

typedef struct {
    bm_algo_q31_t y_hat;
    bm_algo_q31_t disturbance;
} bm_algo_dob_q31_state_t;

void bm_algo_dob_q31_reset(bm_algo_dob_q31_state_t *state);

/**
 * @brief DOB 单步（Q31）：由 u/y 估计扰动
 *
 * @param state 观测器状态（不可为 NULL）
 * @param config 模型增益与低通系数（不可为 NULL）
 * @param u_q31 控制输入
 * @param y_q31 被控输出测量
 * @param disturbance_out 可选扰动估计输出（可为 NULL）
 * @return 扰动估计（Q31）
 */
bm_algo_q31_t bm_algo_dob_q31_step(bm_algo_dob_q31_state_t *state,
                                   const bm_algo_dob_q31_config_t *config,
                                   bm_algo_q31_t u_q31,
                                   bm_algo_q31_t y_q31,
                                   bm_algo_q31_t *disturbance_out);

typedef struct {
    bm_algo_q15_t nominal_capacity_q15;
    bm_algo_q15_t coulomb_efficiency_q15;
    bm_algo_q15_t soc_min;
    bm_algo_q15_t soc_max;
} bm_algo_coulomb_q15_config_t;

typedef struct {
    bm_algo_q15_t soc;
} bm_algo_coulomb_q15_state_t;

void bm_algo_coulomb_q15_reset(bm_algo_coulomb_q15_state_t *state,
                               bm_algo_q15_t soc_init);

/** 库仑 SOC 积分（Q15）：电流与 dt 为 C-rate 与小时比例 */
bm_algo_q15_t bm_algo_coulomb_q15_step(bm_algo_coulomb_q15_state_t *state,
                                       const bm_algo_coulomb_q15_config_t *config,
                                       bm_algo_q15_t current_q15,
                                       bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t b0;
    bm_algo_q31_t b1;
    bm_algo_q31_t a1;
} bm_algo_lead_lag_q31_config_t;

typedef struct {
    bm_algo_q31_t x1;
    bm_algo_q31_t y1;
} bm_algo_lead_lag_q31_state_t;

int bm_algo_lead_lag_q31_init(bm_algo_lead_lag_q31_state_t *state,
                              const bm_algo_lead_lag_q31_config_t *config);

void bm_algo_lead_lag_q31_reset(bm_algo_lead_lag_q31_state_t *state);

bm_algo_q31_t bm_algo_lead_lag_q31_step(bm_algo_lead_lag_q31_state_t *state,
                                        const bm_algo_lead_lag_q31_config_t *config,
                                        bm_algo_q31_t input);

typedef struct {
    bm_algo_q15_t alpha_q15;
} bm_algo_complementary_q15_config_t;

typedef struct {
    bm_algo_q15_t roll_rad;
    bm_algo_q15_t pitch_rad;
} bm_algo_complementary_q15_state_t;

void bm_algo_complementary_q15_reset(bm_algo_complementary_q15_state_t *state);

/**
 * @brief 互补滤波 roll/pitch（Q15）
 *
 * 陀螺积分为 Q15；加速度倾角经 float atan2 桥接（E1 限制，非纯定点）。
 */
void bm_algo_complementary_q15_step(bm_algo_complementary_q15_state_t *state,
                                    const bm_algo_complementary_q15_config_t *config,
                                    bm_algo_q15_t gx_q15,
                                    bm_algo_q15_t gy_q15,
                                    bm_algo_q15_t gz_q15,
                                    bm_algo_q15_t ax_q15,
                                    bm_algo_q15_t ay_q15,
                                    bm_algo_q15_t az_q15,
                                    bm_algo_q15_t dt_q15);

/** 前馈：ref×gain+bias（Q31，无状态） */
bm_algo_q31_t bm_algo_feedforward_q31_step(bm_algo_q31_t reference_q31,
                                         bm_algo_q31_t gain_q31,
                                         bm_algo_q31_t bias_q31);

/** 前馈：ref×gain+bias（Q15，无状态） */
bm_algo_q15_t bm_algo_feedforward_q15_step(bm_algo_q15_t reference_q15,
                                         bm_algo_q15_t gain_q15,
                                         bm_algo_q15_t bias_q15);

/* ---------- 通用定点扩展（第十一批：控制/信号质量） ---------- */
typedef struct {
    bm_algo_q15_t kp;
    bm_algo_q15_t ki;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
    bm_algo_q15_t integrator_min;
    bm_algo_q15_t integrator_max;
} bm_algo_pi_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
    bm_algo_q15_t output;
} bm_algo_pi_q15_state_t;

void bm_algo_pi_q15_reset(bm_algo_pi_q15_state_t *state, bm_algo_q15_t output);

bm_algo_q15_t bm_algo_pi_q15_step(bm_algo_pi_q15_state_t *state,
                                 const bm_algo_pi_q15_config_t *config,
                                 bm_algo_q15_t error,
                                 bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t b0;
    bm_algo_q31_t b1;
    bm_algo_q31_t b2;
    bm_algo_q31_t a1;
    bm_algo_q31_t a2;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
} bm_algo_pr_q31_config_t;

typedef struct {
    bm_algo_q31_t x1;
    bm_algo_q31_t x2;
    bm_algo_q31_t y1;
    bm_algo_q31_t y2;
    bm_algo_q31_t output;
} bm_algo_pr_q31_state_t;

void bm_algo_pr_q31_reset(bm_algo_pr_q31_state_t *state);

bm_algo_q31_t bm_algo_pr_q31_step(bm_algo_pr_q31_state_t *state,
                                  const bm_algo_pr_q31_config_t *config,
                                  bm_algo_q31_t error);

typedef struct {
    bm_algo_q15_t rate_per_s_q15;
} bm_algo_ramp_q15_config_t;

typedef struct {
    bm_algo_q15_t output;
    int done;
} bm_algo_ramp_q15_state_t;

void bm_algo_ramp_q15_reset(bm_algo_ramp_q15_state_t *state, bm_algo_q15_t output);

bm_algo_q15_t bm_algo_ramp_q15_step(bm_algo_ramp_q15_state_t *state,
                                    const bm_algo_ramp_q15_config_t *config,
                                    bm_algo_q15_t target,
                                    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t max_vel_q15;
    bm_algo_q15_t max_accel_q15;
    bm_algo_q15_t max_decel_q15;
} bm_algo_trapezoid_q15_config_t;

typedef struct {
    bm_algo_q15_t position;
    bm_algo_q15_t velocity;
    bm_algo_q15_t target;
    int done;
} bm_algo_trapezoid_q15_state_t;

void bm_algo_trapezoid_q15_reset(bm_algo_trapezoid_q15_state_t *state,
                                 bm_algo_q15_t position,
                                 bm_algo_q15_t velocity);

void bm_algo_trapezoid_q15_set_target(bm_algo_trapezoid_q15_state_t *state,
                                      bm_algo_q15_t target);

bm_algo_q15_t bm_algo_trapezoid_q15_step(bm_algo_trapezoid_q15_state_t *state,
                                         const bm_algo_trapezoid_q15_config_t *config,
                                         bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t tolerance_abs;
    bm_algo_q15_t tolerance_rel;
} bm_algo_redundant_pair_q15_config_t;

/**
 * @brief 冗余通道一致性比较（Q15）
 *
 * @return 故障标志；一致时返回 0
 */
uint32_t bm_algo_redundant_pair_q15_step(bm_algo_q15_t a,
                                         bm_algo_q15_t b,
                                         const bm_algo_redundant_pair_q15_config_t *config);

typedef struct {
    bm_algo_q31_t tolerance_abs;
    bm_algo_q31_t tolerance_rel;
} bm_algo_redundant_pair_q31_config_t;

/**
 * @brief 冗余通道一致性比较（Q31）
 *
 * @return 故障标志；一致时返回 0
 */
uint32_t bm_algo_redundant_pair_q31_step(bm_algo_q31_t a,
                                         bm_algo_q31_t b,
                                         const bm_algo_redundant_pair_q31_config_t *config);

/** 滑动速率估计（Q15，窗口≤16 仅用于内部缓冲占位，E1 为一阶差分） */
typedef struct {
    bm_algo_q15_t prev_input;
    bm_algo_q15_t rate_per_s;
} bm_algo_rate_est_q15_state_t;

void bm_algo_rate_est_q15_reset(bm_algo_rate_est_q15_state_t *state,
                                bm_algo_q15_t input);

bm_algo_q15_t bm_algo_rate_est_q15_step(bm_algo_rate_est_q15_state_t *state,
                                        bm_algo_q15_t input,
                                        bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q15_t ocv_weight;
} bm_algo_soc_fusion_q15_config_t;

/** 两路 SOC 加权融合（Q15） */
bm_algo_q15_t bm_algo_soc_fusion_q15_step(bm_algo_q15_t soc_coulomb,
                                        bm_algo_q15_t soc_ocv,
                                        const bm_algo_soc_fusion_q15_config_t *config);

/* ---------- 通用定点扩展（第十二批：电源/运动/信号质量） ---------- */
typedef struct {
    bm_algo_q15_t max_vel_q15;
    bm_algo_q15_t max_accel_q15;
    bm_algo_q15_t max_jerk_q15;
} bm_algo_scurve_q15_config_t;

typedef struct {
    bm_algo_q15_t position;
    bm_algo_q15_t velocity;
    bm_algo_q15_t acceleration;
    bm_algo_q15_t target;
    int done;
} bm_algo_scurve_q15_state_t;

void bm_algo_scurve_q15_reset(bm_algo_scurve_q15_state_t *state,
                              bm_algo_q15_t position,
                              bm_algo_q15_t velocity,
                              bm_algo_q15_t acceleration);

void bm_algo_scurve_q15_set_target(bm_algo_scurve_q15_state_t *state,
                                   bm_algo_q15_t target);

/** S 曲线单步（Q15，E1 经 float bm_algo_scurve_step 桥接） */
bm_algo_q15_t bm_algo_scurve_q15_step(bm_algo_scurve_q15_state_t *state,
                                      const bm_algo_scurve_q15_config_t *config,
                                      bm_algo_q15_t dt_q15);

/** 滑动速率估计（Q31，一阶差分） */
typedef struct {
    bm_algo_q31_t prev_input;
    bm_algo_q31_t rate_per_s;
} bm_algo_rate_est_q31_state_t;

void bm_algo_rate_est_q31_reset(bm_algo_rate_est_q31_state_t *state,
                                bm_algo_q31_t input);

bm_algo_q31_t bm_algo_rate_est_q31_step(bm_algo_rate_est_q31_state_t *state,
                                        bm_algo_q31_t input,
                                        bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t ocv_weight;
} bm_algo_soc_fusion_q31_config_t;

/** 两路 SOC 加权融合（Q31） */
bm_algo_q31_t bm_algo_soc_fusion_q31_step(bm_algo_q31_t soc_coulomb,
                                          bm_algo_q31_t soc_ocv,
                                          const bm_algo_soc_fusion_q31_config_t *config);

typedef struct {
    bm_algo_q15_t step_v_q15;
    bm_algo_q15_t v_min_q15;
    bm_algo_q15_t v_max_q15;
} bm_algo_mppt_po_q15_config_t;

typedef struct {
    bm_algo_q15_t v_ref_q15;
    bm_algo_q15_t prev_power_q15;
    int           direction;
} bm_algo_mppt_po_q15_state_t;

void bm_algo_mppt_po_q15_reset(bm_algo_mppt_po_q15_state_t *state,
                               bm_algo_q15_t v_init_q15);

bm_algo_q15_t bm_algo_mppt_po_q15_step(bm_algo_mppt_po_q15_state_t *state,
                                     const bm_algo_mppt_po_q15_config_t *config,
                                     bm_algo_q15_t voltage_q15,
                                     bm_algo_q15_t current_q15);

typedef struct {
    bm_algo_q15_t step_v_q15;
    bm_algo_q15_t v_min_q15;
    bm_algo_q15_t v_max_q15;
} bm_algo_mppt_ic_q15_config_t;

typedef struct {
    bm_algo_q15_t v_ref_q15;
    bm_algo_q15_t prev_v_q15;
    bm_algo_q15_t prev_i_q15;
} bm_algo_mppt_ic_q15_state_t;

void bm_algo_mppt_ic_q15_reset(bm_algo_mppt_ic_q15_state_t *state,
                               bm_algo_q15_t v_init_q15);

bm_algo_q15_t bm_algo_mppt_ic_q15_step(bm_algo_mppt_ic_q15_state_t *state,
                                     const bm_algo_mppt_ic_q15_config_t *config,
                                     bm_algo_q15_t voltage_q15,
                                     bm_algo_q15_t current_q15);

typedef struct {
    bm_algo_q15_t min_v_q15;
    bm_algo_q15_t max_v_q15;
    bm_algo_q15_t max_rate_per_s_q15;
} bm_algo_range_monitor_q15_config_t;

typedef struct {
    bm_algo_q15_t prev_q15;
    uint32_t      fault_flags;
} bm_algo_range_monitor_q15_state_t;

void bm_algo_range_monitor_q15_reset(bm_algo_range_monitor_q15_state_t *state,
                                     bm_algo_q15_t v_q15);

/**
 * @brief 范围/变化率/冻结监控（Q15）
 *
 * @return 故障标志位（与 bm_algo_range_monitor_step 语义一致）
 */
uint32_t bm_algo_range_monitor_q15_step(
    bm_algo_range_monitor_q15_state_t *state,
    const bm_algo_range_monitor_q15_config_t *config,
    bm_algo_q15_t sample_q15,
    bm_algo_q15_t dt_q15);

typedef struct {
    uint32_t      stable_count_required;
    bm_algo_q15_t tolerance_q15;
} bm_algo_debounce_analog_q15_config_t;

typedef struct {
    bm_algo_q15_t candidate_q15;
    uint32_t      stable_count;
    bm_algo_q15_t latched_q15;
    int           valid;
} bm_algo_debounce_analog_q15_state_t;

void bm_algo_debounce_analog_q15_reset(bm_algo_debounce_analog_q15_state_t *state,
                                       bm_algo_q15_t initial_q15);

/**
 * @brief 模拟量去抖（Q15）
 *
 * @return 稳定后返回 1 并更新 latched；否则 0
 */
int bm_algo_debounce_analog_q15_step(
    bm_algo_debounce_analog_q15_state_t *state,
    const bm_algo_debounce_analog_q15_config_t *config,
    bm_algo_q15_t sample_q15);

typedef struct {
    bm_algo_q31_t accumulated_wh_q31;
} bm_algo_energy_wh_q15_state_t;

void bm_algo_energy_wh_q15_reset(bm_algo_energy_wh_q15_state_t *state);

/**
 * @brief 有功电能 Wh 积分（Q15 功率×步长，内部 Q31 累加）
 *
 * @return 累计 Wh（Q31，与 float 版物理量纲一致需按标定换算）
 */
bm_algo_q31_t bm_algo_energy_wh_integrator_q15_step(
    bm_algo_energy_wh_q15_state_t *state,
    bm_algo_q15_t p_q15,
    bm_algo_q15_t dt_q15);

/* ---------- 通用定点扩展（第十四批：全族 Q15/Q31 收口） ---------- */
typedef struct {
    bm_algo_q31_t alpha_q31;
} bm_algo_complementary_q31_config_t;

typedef struct {
    bm_algo_q31_t roll_rad;
    bm_algo_q31_t pitch_rad;
} bm_algo_complementary_q31_state_t;

void bm_algo_complementary_q31_reset(bm_algo_complementary_q31_state_t *state);

/** 互补滤波 roll/pitch（Q31）；加速度倾角经 float atan2 桥接（E1） */
void bm_algo_complementary_q31_step(bm_algo_complementary_q31_state_t *state,
                                    const bm_algo_complementary_q31_config_t *config,
                                    bm_algo_q31_t gx_q31,
                                    bm_algo_q31_t gy_q31,
                                    bm_algo_q31_t gz_q31,
                                    bm_algo_q31_t ax_q31,
                                    bm_algo_q31_t ay_q31,
                                    bm_algo_q31_t az_q31,
                                    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t x0_q15;
    bm_algo_q15_t y0_q15;
    bm_algo_q15_t x1_q15;
    bm_algo_q15_t y1_q15;
    bm_algo_q15_t step_size_q15;
} bm_algo_dda_q15_config_t;

typedef struct {
    bm_algo_q15_t x_q15;
    bm_algo_q15_t y_q15;
    int done;
    float x;
    float y;
    float err;
    int step_x;
    int step_y;
    float dx;
    float dy;
    float target_x;
    float target_y;
    float step_size;
    uint32_t steps;
    uint32_t step_count;
} bm_algo_dda_q15_state_t;

void bm_algo_dda_q15_reset(bm_algo_dda_q15_state_t *state,
                          const bm_algo_dda_q15_config_t *config);

/** DDA 直线插补（Q15，E1 经 float bm_algo_dda_step 桥接） */
int bm_algo_dda_q15_step(bm_algo_dda_q15_state_t *state,
                         const bm_algo_dda_q15_config_t *config,
                         bm_algo_q15_t *x_out_q15,
                         bm_algo_q15_t *y_out_q15);

typedef struct {
    bm_algo_q31_t x0_q31;
    bm_algo_q31_t y0_q31;
    bm_algo_q31_t x1_q31;
    bm_algo_q31_t y1_q31;
    bm_algo_q31_t step_size_q31;
} bm_algo_dda_q31_config_t;

typedef struct {
    bm_algo_q31_t x_q31;
    bm_algo_q31_t y_q31;
    int done;
    float x;
    float y;
    float err;
    int step_x;
    int step_y;
    float dx;
    float dy;
    float target_x;
    float target_y;
    float step_size;
    uint32_t steps;
    uint32_t step_count;
} bm_algo_dda_q31_state_t;

void bm_algo_dda_q31_reset(bm_algo_dda_q31_state_t *state,
                          const bm_algo_dda_q31_config_t *config);

/** DDA 直线插补（Q31，E1 经 float bm_algo_dda_step 桥接） */
int bm_algo_dda_q31_step(bm_algo_dda_q31_state_t *state,
                         const bm_algo_dda_q31_config_t *config,
                         bm_algo_q31_t *x_out_q31,
                         bm_algo_q31_t *y_out_q31);

typedef struct {
    uint32_t stable_count_required;
    bm_algo_q31_t tolerance_q31;
} bm_algo_debounce_analog_q31_config_t;

typedef struct {
    bm_algo_q31_t candidate_q31;
    uint32_t      stable_count;
    bm_algo_q31_t latched_q31;
    int           valid;
} bm_algo_debounce_analog_q31_state_t;

void bm_algo_debounce_analog_q31_reset(bm_algo_debounce_analog_q31_state_t *state,
                                       bm_algo_q31_t initial_q31);

int bm_algo_debounce_analog_q31_step(
    bm_algo_debounce_analog_q31_state_t *state,
    const bm_algo_debounce_analog_q31_config_t *config,
    bm_algo_q31_t sample_q31);

typedef struct {
    uint32_t decim;
    uint32_t counter;
} bm_algo_decimator_q15_state_t;

void bm_algo_decimator_q15_reset(bm_algo_decimator_q15_state_t *state);

int bm_algo_decimator_q15_step(bm_algo_decimator_q15_state_t *state,
                               uint32_t decim,
                               bm_algo_q15_t input_q15,
                               bm_algo_q15_t *output_q15);

typedef struct {
    uint32_t decim;
    uint32_t counter;
} bm_algo_decimator_q31_state_t;

void bm_algo_decimator_q31_reset(bm_algo_decimator_q31_state_t *state);

int bm_algo_decimator_q31_step(bm_algo_decimator_q31_state_t *state,
                               uint32_t decim,
                               bm_algo_q31_t input_q31,
                               bm_algo_q31_t *output_q31);

typedef struct {
    int32_t max_delta_per_step;
} bm_algo_encoder_diag_q15_config_t;

typedef struct {
    int32_t prev_count;
} bm_algo_encoder_diag_q15_state_t;

void bm_algo_encoder_diag_q15_reset(bm_algo_encoder_diag_q15_state_t *state,
                                    int32_t raw_count);

uint32_t bm_algo_encoder_diag_q15_step(bm_algo_encoder_diag_q15_state_t *state,
                                     const bm_algo_encoder_diag_q15_config_t *config,
                                     int32_t raw_count,
                                     int index_pulse_seen);

typedef struct {
    int32_t max_delta_per_step;
} bm_algo_encoder_diag_q31_config_t;

typedef struct {
    int32_t prev_count;
} bm_algo_encoder_diag_q31_state_t;

void bm_algo_encoder_diag_q31_reset(bm_algo_encoder_diag_q31_state_t *state,
                                    int32_t raw_count);

uint32_t bm_algo_encoder_diag_q31_step(bm_algo_encoder_diag_q31_state_t *state,
                                     const bm_algo_encoder_diag_q31_config_t *config,
                                     int32_t raw_count,
                                     int index_pulse_seen);

typedef struct {
    bm_algo_q31_t accumulated_wh_q31;
} bm_algo_energy_wh_q31_state_t;

void bm_algo_energy_wh_q31_reset(bm_algo_energy_wh_q31_state_t *state);

bm_algo_q31_t bm_algo_energy_wh_integrator_q31_step(
    bm_algo_energy_wh_q31_state_t *state,
    bm_algo_q31_t p_q31,
    bm_algo_q31_t dt_q31);

#define BM_ALGO_FIR_Q15_MAX_TAPS  8u

typedef struct {
    const bm_algo_q15_t *coeffs;
    uint8_t              tap_count;
    bm_algo_q15_t       *delay_line;
} bm_algo_fir_q15_config_t;

typedef struct {
    uint8_t index;
    uint8_t tap_count;
} bm_algo_fir_q15_state_t;

int bm_algo_fir_q15_init(bm_algo_fir_q15_state_t *state,
                         const bm_algo_fir_q15_config_t *config);

void bm_algo_fir_q15_reset(bm_algo_fir_q15_state_t *state,
                           const bm_algo_fir_q15_config_t *config);

bm_algo_q15_t bm_algo_fir_q15_step(bm_algo_fir_q15_state_t *state,
                                   const bm_algo_fir_q15_config_t *config,
                                   bm_algo_q15_t input_q15);

#define BM_ALGO_FIR_Q31_MAX_TAPS  8u

typedef struct {
    const bm_algo_q31_t *coeffs;
    uint8_t              tap_count;
    bm_algo_q31_t       *delay_line;
} bm_algo_fir_q31_config_t;

typedef struct {
    uint8_t index;
    uint8_t tap_count;
} bm_algo_fir_q31_state_t;

int bm_algo_fir_q31_init(bm_algo_fir_q31_state_t *state,
                         const bm_algo_fir_q31_config_t *config);

void bm_algo_fir_q31_reset(bm_algo_fir_q31_state_t *state,
                           const bm_algo_fir_q31_config_t *config);

bm_algo_q31_t bm_algo_fir_q31_step(bm_algo_fir_q31_state_t *state,
                                   const bm_algo_fir_q31_config_t *config,
                                   bm_algo_q31_t input_q31);

typedef struct {
    bm_algo_q15_t rs_q15;
    bm_algo_q15_t ls_q15;
    bm_algo_q15_t pll_kp_q15;
    bm_algo_q15_t pll_ki_q15;
} bm_algo_flux_observer_q15_config_t;

typedef struct {
    bm_algo_q15_t theta_rad_q15;
    bm_algo_q15_t omega_rad_s_q15;
    float theta_rad;
    float omega_rad_s;
    float flux_alpha;
    float flux_beta;
} bm_algo_flux_observer_q15_state_t;

void bm_algo_flux_observer_q15_reset(bm_algo_flux_observer_q15_state_t *state,
                                     bm_algo_q15_t theta_rad_q15);

/** 磁链观测（Q15，E1 经 float bm_algo_flux_observer_step 桥接） */
bm_algo_q15_t bm_algo_flux_observer_q15_step(
    bm_algo_flux_observer_q15_state_t *state,
    const bm_algo_flux_observer_q15_config_t *config,
    bm_algo_q15_t v_alpha_q15,
    bm_algo_q15_t v_beta_q15,
    bm_algo_q15_t i_alpha_q15,
    bm_algo_q15_t i_beta_q15,
    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t rs_q31;
    bm_algo_q31_t ls_q31;
    bm_algo_q31_t pll_kp_q31;
    bm_algo_q31_t pll_ki_q31;
} bm_algo_flux_observer_q31_config_t;

typedef struct {
    bm_algo_q31_t theta_rad_q31;
    bm_algo_q31_t omega_rad_s_q31;
    float theta_rad;
    float omega_rad_s;
    float flux_alpha;
    float flux_beta;
} bm_algo_flux_observer_q31_state_t;

void bm_algo_flux_observer_q31_reset(bm_algo_flux_observer_q31_state_t *state,
                                     bm_algo_q31_t theta_rad_q31);

/** 磁链观测（Q31，E1 经 float bm_algo_flux_observer_step 桥接） */
bm_algo_q31_t bm_algo_flux_observer_q31_step(
    bm_algo_flux_observer_q31_state_t *state,
    const bm_algo_flux_observer_q31_config_t *config,
    bm_algo_q31_t v_alpha_q31,
    bm_algo_q31_t v_beta_q31,
    bm_algo_q31_t i_alpha_q31,
    bm_algo_q31_t i_beta_q31,
    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t ratio_q15;
    bm_algo_q15_t phase_q15;
    bm_algo_q15_t prev_sample_q15;
} bm_algo_linear_resampler_q15_state_t;

void bm_algo_linear_resampler_q15_reset(bm_algo_linear_resampler_q15_state_t *state,
                                        bm_algo_q15_t ratio_q15,
                                        bm_algo_q15_t initial_q15);

int bm_algo_linear_resampler_q15_step(bm_algo_linear_resampler_q15_state_t *state,
                                      bm_algo_q15_t input_q15,
                                      bm_algo_q15_t *outputs_q15,
                                      uint32_t max_outputs,
                                      uint32_t *out_count);

typedef struct {
    bm_algo_q31_t ratio_q31;
    bm_algo_q31_t phase_q31;
    bm_algo_q31_t prev_sample_q31;
} bm_algo_linear_resampler_q31_state_t;

void bm_algo_linear_resampler_q31_reset(bm_algo_linear_resampler_q31_state_t *state,
                                        bm_algo_q31_t ratio_q31,
                                        bm_algo_q31_t initial_q31);

int bm_algo_linear_resampler_q31_step(bm_algo_linear_resampler_q31_state_t *state,
                                      bm_algo_q31_t input_q31,
                                      bm_algo_q31_t *outputs_q31,
                                      uint32_t max_outputs,
                                      uint32_t *out_count);

typedef struct {
    bm_algo_q15_t beta_q15;
} bm_algo_madgwick_q15_config_t;

typedef struct {
    bm_algo_q15_t qw_q15;
    bm_algo_q15_t qx_q15;
    bm_algo_q15_t qy_q15;
    bm_algo_q15_t qz_q15;
} bm_algo_madgwick_q15_state_t;

void bm_algo_madgwick_q15_reset(bm_algo_madgwick_q15_state_t *state);

/** Madgwick AHRS（Q15，E1 经 float bm_algo_madgwick_step 桥接） */
void bm_algo_madgwick_q15_step(bm_algo_madgwick_q15_state_t *state,
                               const bm_algo_madgwick_q15_config_t *config,
                               bm_algo_q15_t gx_q15,
                               bm_algo_q15_t gy_q15,
                               bm_algo_q15_t gz_q15,
                               bm_algo_q15_t ax_q15,
                               bm_algo_q15_t ay_q15,
                               bm_algo_q15_t az_q15,
                               bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t beta_q31;
} bm_algo_madgwick_q31_config_t;

typedef struct {
    bm_algo_q31_t qw_q31;
    bm_algo_q31_t qx_q31;
    bm_algo_q31_t qy_q31;
    bm_algo_q31_t qz_q31;
} bm_algo_madgwick_q31_state_t;

void bm_algo_madgwick_q31_reset(bm_algo_madgwick_q31_state_t *state);

/** Madgwick AHRS（Q31，E1 经 float bm_algo_madgwick_step 桥接） */
void bm_algo_madgwick_q31_step(bm_algo_madgwick_q31_state_t *state,
                               const bm_algo_madgwick_q31_config_t *config,
                               bm_algo_q31_t gx_q31,
                               bm_algo_q31_t gy_q31,
                               bm_algo_q31_t gz_q31,
                               bm_algo_q31_t ax_q31,
                               bm_algo_q31_t ay_q31,
                               bm_algo_q31_t az_q31,
                               bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t kp_q15;
    bm_algo_q15_t ki_q15;
} bm_algo_mahony_q15_config_t;

typedef struct {
    bm_algo_q15_t qw_q15;      /**< 四元数 w 分量（Q15） */
    bm_algo_q15_t qx_q15;      /**< 四元数 x 分量（Q15） */
    bm_algo_q15_t qy_q15;      /**< 四元数 y 分量（Q15） */
    bm_algo_q15_t qz_q15;      /**< 四元数 z 分量（Q15） */
    float integral_x;           /**< Ki 积分项 x（浮点保存帧间状态，对应 bm_algo_mahony_state_t.integral_x） */
    float integral_y;           /**< Ki 积分项 y（浮点保存帧间状态） */
    float integral_z;           /**< Ki 积分项 z（浮点保存帧间状态） */
} bm_algo_mahony_q15_state_t;

void bm_algo_mahony_q15_reset(bm_algo_mahony_q15_state_t *state);

/** Mahony AHRS（Q15，E1 经 float bm_algo_mahony_step 桥接） */
void bm_algo_mahony_q15_step(bm_algo_mahony_q15_state_t *state,
                             const bm_algo_mahony_q15_config_t *config,
                             bm_algo_q15_t gx_q15,
                             bm_algo_q15_t gy_q15,
                             bm_algo_q15_t gz_q15,
                             bm_algo_q15_t ax_q15,
                             bm_algo_q15_t ay_q15,
                             bm_algo_q15_t az_q15,
                             bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t kp_q31;
    bm_algo_q31_t ki_q31;
} bm_algo_mahony_q31_config_t;

typedef struct {
    bm_algo_q31_t qw_q31;      /**< 四元数 w 分量（Q31） */
    bm_algo_q31_t qx_q31;      /**< 四元数 x 分量（Q31） */
    bm_algo_q31_t qy_q31;      /**< 四元数 y 分量（Q31） */
    bm_algo_q31_t qz_q31;      /**< 四元数 z 分量（Q31） */
    float integral_x;           /**< Ki 积分项 x（浮点保存帧间状态，对应 bm_algo_mahony_state_t.integral_x） */
    float integral_y;           /**< Ki 积分项 y（浮点保存帧间状态） */
    float integral_z;           /**< Ki 积分项 z（浮点保存帧间状态） */
} bm_algo_mahony_q31_state_t;

void bm_algo_mahony_q31_reset(bm_algo_mahony_q31_state_t *state);

/** Mahony AHRS（Q31，E1 经 float bm_algo_mahony_step 桥接） */
void bm_algo_mahony_q31_step(bm_algo_mahony_q31_state_t *state,
                             const bm_algo_mahony_q31_config_t *config,
                             bm_algo_q31_t gx_q31,
                             bm_algo_q31_t gy_q31,
                             bm_algo_q31_t gz_q31,
                             bm_algo_q31_t ax_q31,
                             bm_algo_q31_t ay_q31,
                             bm_algo_q31_t az_q31,
                             bm_algo_q31_t dt_q31);

#define BM_ALGO_MEDIAN_Q15_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_median_q15_config_t;

typedef struct {
    bm_algo_q15_t samples[BM_ALGO_MEDIAN_Q15_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_median_q15_state_t;

void bm_algo_median_q15_reset(bm_algo_median_q15_state_t *state);

bm_algo_q15_t bm_algo_median_q15_step(bm_algo_median_q15_state_t *state,
                                      const bm_algo_median_q15_config_t *config,
                                      bm_algo_q15_t input_q15);

#define BM_ALGO_MEDIAN_Q31_MAX  16u

typedef struct {
    uint16_t window_size;
} bm_algo_median_q31_config_t;

typedef struct {
    bm_algo_q31_t samples[BM_ALGO_MEDIAN_Q31_MAX];
    uint16_t      count;
    uint16_t      index;
} bm_algo_median_q31_state_t;

void bm_algo_median_q31_reset(bm_algo_median_q31_state_t *state);

bm_algo_q31_t bm_algo_median_q31_step(bm_algo_median_q31_state_t *state,
                                      const bm_algo_median_q31_config_t *config,
                                      bm_algo_q31_t input_q31);

typedef struct {
    bm_algo_q31_t step_v_q31;
    bm_algo_q31_t v_min_q31;
    bm_algo_q31_t v_max_q31;
} bm_algo_mppt_po_q31_config_t;

typedef struct {
    bm_algo_q31_t v_ref_q31;
    bm_algo_q31_t prev_power_q31;
    int           direction;
} bm_algo_mppt_po_q31_state_t;

void bm_algo_mppt_po_q31_reset(bm_algo_mppt_po_q31_state_t *state,
                               bm_algo_q31_t v_init_q31);

bm_algo_q31_t bm_algo_mppt_po_q31_step(bm_algo_mppt_po_q31_state_t *state,
                                       const bm_algo_mppt_po_q31_config_t *config,
                                       bm_algo_q31_t voltage_q31,
                                       bm_algo_q31_t current_q31);

typedef struct {
    bm_algo_q31_t step_v_q31;
    bm_algo_q31_t v_min_q31;
    bm_algo_q31_t v_max_q31;
} bm_algo_mppt_ic_q31_config_t;

typedef struct {
    bm_algo_q31_t v_ref_q31;
    bm_algo_q31_t prev_v_q31;
    bm_algo_q31_t prev_i_q31;
} bm_algo_mppt_ic_q31_state_t;

void bm_algo_mppt_ic_q31_reset(bm_algo_mppt_ic_q31_state_t *state,
                               bm_algo_q31_t v_init_q31);

bm_algo_q31_t bm_algo_mppt_ic_q31_step(bm_algo_mppt_ic_q31_state_t *state,
                                       const bm_algo_mppt_ic_q31_config_t *config,
                                       bm_algo_q31_t voltage_q31,
                                       bm_algo_q31_t current_q31);

typedef struct {
    bm_algo_q15_t kp_q15;
    bm_algo_q15_t ki_q15;
    bm_algo_q15_t kd_q15;
    bm_algo_q15_t b_q15;
    bm_algo_q15_t out_min;
    bm_algo_q15_t out_max;
    bm_algo_q15_t integrator_min;
    bm_algo_q15_t integrator_max;
    bm_algo_q15_t d_filter_coeff_q15;
} bm_algo_pid2_q15_config_t;

typedef struct {
    bm_algo_q15_t integrator;
    bm_algo_q15_t prev_measurement;
    bm_algo_q15_t d_filtered;
    bm_algo_q15_t output;
} bm_algo_pid2_q15_state_t;

void bm_algo_pid2_q15_reset(bm_algo_pid2_q15_state_t *state, bm_algo_q15_t output);

bm_algo_q15_t bm_algo_pid2_q15_step(bm_algo_pid2_q15_state_t *state,
                                    const bm_algo_pid2_q15_config_t *config,
                                    bm_algo_q15_t reference_q15,
                                    bm_algo_q15_t measurement_q15,
                                    bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t kp_q31;
    bm_algo_q31_t ki_q31;
    bm_algo_q31_t kd_q31;
    bm_algo_q31_t b_q31;
    bm_algo_q31_t out_min;
    bm_algo_q31_t out_max;
    bm_algo_q31_t integrator_min;
    bm_algo_q31_t integrator_max;
    bm_algo_q31_t d_filter_alpha_q31;
} bm_algo_pid2_q31_config_t;

typedef struct {
    bm_algo_q31_t integrator;
    bm_algo_q31_t prev_measurement;
    bm_algo_q31_t d_filtered;
    bm_algo_q31_t output;
} bm_algo_pid2_q31_state_t;

void bm_algo_pid2_q31_reset(bm_algo_pid2_q31_state_t *state, bm_algo_q31_t output);

bm_algo_q31_t bm_algo_pid2_q31_step(bm_algo_pid2_q31_state_t *state,
                                    const bm_algo_pid2_q31_config_t *config,
                                    bm_algo_q31_t reference_q31,
                                    bm_algo_q31_t measurement_q31,
                                    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q31_t min_v_q31;
    bm_algo_q31_t max_v_q31;
    bm_algo_q31_t max_rate_per_s_q31;
} bm_algo_range_monitor_q31_config_t;

typedef struct {
    bm_algo_q31_t prev_q31;
    uint32_t      fault_flags;
} bm_algo_range_monitor_q31_state_t;

void bm_algo_range_monitor_q31_reset(bm_algo_range_monitor_q31_state_t *state,
                                     bm_algo_q31_t v_q31);

uint32_t bm_algo_range_monitor_q31_step(
    bm_algo_range_monitor_q31_state_t *state,
    const bm_algo_range_monitor_q31_config_t *config,
    bm_algo_q31_t sample_q31,
    bm_algo_q31_t dt_q31);

typedef struct {
    bm_algo_q15_t model_gain_q15;
    uint32_t      delay_steps;
} bm_algo_smith_predictor_q15_config_t;

typedef struct {
    bm_algo_q15_t *u_delay_line_q15;
    uint32_t       line_len;
    uint32_t       delay_steps;
    uint32_t       head;
} bm_algo_smith_predictor_q15_state_t;

int bm_algo_smith_predictor_q15_init(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config,
    bm_algo_q15_t *delay_line_q15,
    uint32_t line_len);

void bm_algo_smith_predictor_q15_reset(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config);

/** Smith 预估器（Q15，E1 经 float bm_algo_smith_predictor_step 桥接） */
bm_algo_q15_t bm_algo_smith_predictor_q15_step(
    bm_algo_smith_predictor_q15_state_t *state,
    const bm_algo_smith_predictor_q15_config_t *config,
    bm_algo_q15_t reference_q15,
    bm_algo_q15_t measurement_q15,
    bm_algo_q15_t u_controller_q15);

typedef struct {
    bm_algo_q31_t model_gain_q31;
    uint32_t      delay_steps;
} bm_algo_smith_predictor_q31_config_t;

typedef struct {
    bm_algo_q31_t *u_delay_line_q31;
    uint32_t       line_len;
    uint32_t       delay_steps;
    uint32_t       head;
} bm_algo_smith_predictor_q31_state_t;

int bm_algo_smith_predictor_q31_init(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config,
    bm_algo_q31_t *delay_line_q31,
    uint32_t line_len);

void bm_algo_smith_predictor_q31_reset(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config);

/** Smith 预估器（Q31，E1 经 float bm_algo_smith_predictor_step 桥接） */
bm_algo_q31_t bm_algo_smith_predictor_q31_step(
    bm_algo_smith_predictor_q31_state_t *state,
    const bm_algo_smith_predictor_q31_config_t *config,
    bm_algo_q31_t reference_q31,
    bm_algo_q31_t measurement_q31,
    bm_algo_q31_t u_controller_q31);

typedef struct {
    bm_algo_q15_t nominal_omega_q15;
    bm_algo_q15_t k_sogi_q15;
    bm_algo_q15_t k_pll_q15;
} bm_algo_sogi_pll_q15_config_t;

typedef struct {
    bm_algo_q15_t theta_rad_q15;
    bm_algo_q15_t omega_rad_s_q15;
    float theta_rad;
    float omega_rad_s;
    float v_alpha;
    float v_beta;
    float integrator;
} bm_algo_sogi_pll_q15_state_t;

void bm_algo_sogi_pll_q15_reset(bm_algo_sogi_pll_q15_state_t *state,
                                const bm_algo_sogi_pll_q15_config_t *config);

/** SOGI-PLL（Q15，E1 经 float bm_algo_sogi_pll_step 桥接） */
void bm_algo_sogi_pll_q15_step(bm_algo_sogi_pll_q15_state_t *state,
                               const bm_algo_sogi_pll_q15_config_t *config,
                               bm_algo_q15_t v_input_q15,
                               bm_algo_q15_t dt_q15);

typedef struct {
    bm_algo_q31_t nominal_omega_q31;
    bm_algo_q31_t k_sogi_q31;
    bm_algo_q31_t k_pll_q31;
} bm_algo_sogi_pll_q31_config_t;

typedef struct {
    bm_algo_q31_t theta_rad_q31;
    bm_algo_q31_t omega_rad_s_q31;
    float theta_rad;
    float omega_rad_s;
    float v_alpha;
    float v_beta;
    float integrator;
} bm_algo_sogi_pll_q31_state_t;

void bm_algo_sogi_pll_q31_reset(bm_algo_sogi_pll_q31_state_t *state,
                                const bm_algo_sogi_pll_q31_config_t *config);

/** SOGI-PLL（Q31，E1 经 float bm_algo_sogi_pll_step 桥接） */
void bm_algo_sogi_pll_q31_step(bm_algo_sogi_pll_q31_state_t *state,
                               const bm_algo_sogi_pll_q31_config_t *config,
                               bm_algo_q31_t v_input_q31,
                               bm_algo_q31_t dt_q31);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FIXED_H */

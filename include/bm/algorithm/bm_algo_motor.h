/**
 * @file bm_algo_motor.h
 * @brief 电机纯数学核：Clarke/Park 变换与 SVPWM
 *
 * 采用幅值不变 Clarke 变换；角度为电角度（弧度）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            PWM 扇区采样窗口判定
 * 2026-06-23       1.2            zeh            磁链观测器纯积分改为带衰减积分，新增 flux_observer_wc_rad_s 配置字段
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_MOTOR_H
#define BM_ALGO_MOTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ia;
    float ib;
    float ic;
} bm_algo_abc_t;

typedef struct {
    float i_alpha;
    float i_beta;
} bm_algo_alphabeta_t;

typedef struct {
    float id;
    float iq;
} bm_algo_dq_t;

typedef struct {
    float duty_a;
    float duty_b;
    float duty_c;
} bm_algo_svpwm_out_t;

/** Clarke：三相 → αβ（幅值不变） */
void bm_algo_clarke(const bm_algo_abc_t *abc, bm_algo_alphabeta_t *ab);

/** 两相电流 Clarke（假定 ia+ib+ic=0，仅用 ia/ib） */
void bm_algo_clarke_2shunt(float ia, float ib, bm_algo_alphabeta_t *ab);

/** Park：αβ → dq */
void bm_algo_park(const bm_algo_alphabeta_t *ab,
                  float theta_rad,
                  bm_algo_dq_t *dq);

/** 逆 Park：dq → αβ */
void bm_algo_inv_park(const bm_algo_dq_t *dq,
                      float theta_rad,
                      bm_algo_alphabeta_t *ab);

/** 逆 Clarke：αβ → 三相 */
void bm_algo_inv_clarke(const bm_algo_alphabeta_t *ab, bm_algo_abc_t *abc);

/**
 * SVPWM：αβ 电压参考 → 三相占空比 [0,1]
 * @param v_alpha,v_beta  per-unit 电压（相对直流母线）
 * @param vbus_v          母线电压（V），用于归一化；若已 per-unit 可传 1
 */
/* v_alpha/v_beta and vbus_v must use the same voltage unit. */
void bm_algo_svpwm(float v_alpha,
                   float v_beta,
                   float vbus_v,
                   bm_algo_svpwm_out_t *out);

/**
 * @brief SVPWM 过调制（E1：线性区内标准 SVPWM，超限按比例缩至六脉冲边界）
 *
 * @param max_linear_mod 线性调制比上限（相对 vbus，典型约 0.577）
 */
void bm_algo_svpwm_overmod(float v_alpha,
                           float v_beta,
                           float vbus_v,
                           float max_linear_mod,
                           bm_algo_svpwm_out_t *out);

/** 限制 dq 电压矢量幅值（圆限幅） */
void bm_algo_voltage_limit(float *vd, float *vq, float v_max);

/** 双电阻采样重构三相电流（ic = -ia - ib） */
void bm_algo_current_from_2shunt(float ia, float ib, bm_algo_abc_t *abc);

/**
 * @deprecated 缺少 PWM 周期，无法进行量纲正确的补偿，仅原样返回 phase_v（空操作直通）。
 *             请改用 bm_algo_deadtime_comp_v_period()（补偿量 = sign(I)·Vbus·deadtime/period）。
 */
float bm_algo_deadtime_comp_v(float phase_v,
                              float phase_current_a,
                              float deadtime_s,
                              float vbus_v);

/** 死区压降补偿，补偿量为 sign(I) * Vbus * deadtime / PWM period。 */
float bm_algo_deadtime_comp_v_period(float phase_v,
                                     float phase_current_a,
                                     float deadtime_s,
                                     float pwm_period_s,
                                     float vbus_v);

/* ---------- 无感 FOC 辅助（K0） ---------- */
typedef struct {
    float rs_ohm;   /**< 定子电阻（Ω） */
    float ls_h;     /**< 定子电感（H） */
    float pll_kp;   /**< PLL 比例增益 */
    float pll_ki;   /**< PLL 积分增益 */
    /**
     * @brief 磁链积分衰减截止频率（rad/s）
     *
     * 用于带衰减积分：flux = flux*(1 - wc*dt) + v_emf*dt，
     * 消除纯积分在低速/静止时的 DC 偏置漂移。
     * 典型取值：5～30 rad/s（对应截止频率约 0.8～5 Hz）；
     * 设为 0 时退化为纯积分（不推荐）。
     */
    float flux_observer_wc_rad_s;
} bm_algo_flux_observer_config_t;

typedef struct {
    float theta_rad;
    float omega_rad_s;
    float flux_alpha;
    float flux_beta;
} bm_algo_flux_observer_state_t;

void bm_algo_flux_observer_reset(bm_algo_flux_observer_state_t *state,
                                 float theta_rad);

/**
 * 磁链观测 + PLL，返回电角度（rad）
 * 定子磁链：ψ = ∫(V - Rs·I)dt - Ls·I
 */
float bm_algo_flux_observer_step(bm_algo_flux_observer_state_t *state,
                                 const bm_algo_flux_observer_config_t *config,
                                 float v_alpha,
                                 float v_beta,
                                 float i_alpha,
                                 float i_beta,
                                 float dt_s);

/** MTPA：由 iq 参考求 id 参考（简化 IPM 模型） */
float bm_algo_mtpa_id_ref(float iq_ref_a,
                          float ld_h,
                          float lq_h,
                          float psi_f_wb);

/** 弱磁：电压饱和时下调 id 参考 */
float bm_algo_fw_id_adjust(float id_ref_a, float vd, float vq, float v_max_pu);

/**
 * @brief PWM 扇区采样窗口有效性判定
 *
 * 六扇区 SVPWM：每扇区 60°，adc_phase_deg 为电角度相位 [0,360)；
 * window_deg 为允许采样窗口半宽（度）。
 *
 * @param sector 当前扇区（0–5）
 * @param adc_phase_deg ADC 触发相位（度）
 * @param window_deg 有效窗口半宽（度，>0）
 * @return 1 有效；0 无效或参数错误
 */
int bm_algo_pwm_sample_window_valid(uint32_t sector,
                                    float adc_phase_deg,
                                    float window_deg);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_MOTOR_H */

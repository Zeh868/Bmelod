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
 * 2026-08-01       1.2            zeh          补齐公共 API 中文 Doxygen
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

/**
 * @brief 将三相静止坐标量变换到 alpha/beta 坐标系（幅值不变）。
 * @param abc 三相坐标量。
 * @param ab alpha/beta 坐标量。
 */
void bm_algo_clarke(const bm_algo_abc_t *abc, bm_algo_alphabeta_t *ab);

/**
 * @brief 假定三相电流和为零，根据 A、B 两相采样电流执行 Clarke 变换。
 * @param ia A 相采样电流。
 * @param ib B 相采样电流。
 * @param ab alpha/beta 坐标量。
 */
void bm_algo_clarke_2shunt(float ia, float ib, bm_algo_alphabeta_t *ab);

/**
 * @brief 将 alpha/beta 坐标量变换到旋转 d/q 坐标系。
 * @param ab alpha/beta 坐标量。
 * @param theta_rad 电角度，单位 rad。
 * @param dq d/q 轴坐标量。
 */
void bm_algo_park(const bm_algo_alphabeta_t *ab,
                  float theta_rad,
                  bm_algo_dq_t *dq);

/**
 * @brief 将旋转 d/q 坐标量变换到 alpha/beta 坐标系。
 * @param dq d/q 轴坐标量。
 * @param theta_rad 电角度，单位 rad。
 * @param ab alpha/beta 坐标量。
 */
void bm_algo_inv_park(const bm_algo_dq_t *dq,
                      float theta_rad,
                      bm_algo_alphabeta_t *ab);

/**
 * @brief 将 alpha/beta 坐标量逆变换为三相坐标量。
 * @param ab alpha/beta 坐标量。
 * @param abc 三相坐标量。
 */
void bm_algo_inv_clarke(const bm_algo_alphabeta_t *ab, bm_algo_abc_t *abc);

/**
 * SVPWM：αβ 电压参考 → 三相占空比 [0,1]
 * @param v_alpha,v_beta  per-unit 电压（相对直流母线）
 * @param vbus_v          母线电压（V），用于归一化；若已 per-unit 可传 1
 */
/* v_alpha/v_beta and vbus_v must use the same voltage unit. */
/**
 * @brief 根据电压矢量和母线电压计算 SVPWM 占空比。
 * @param v_alpha alpha 轴电压分量。
 * @param v_beta beta 轴电压分量。
 * @param vbus_v 直流母线电压，单位 V。
 * @param out 输出的调制结果。
 */
void bm_algo_svpwm(float v_alpha,
                   float v_beta,
                   float vbus_v,
                   bm_algo_svpwm_out_t *out);

/**
 * @brief SVPWM 过调制（E1：线性区内标准 SVPWM，超限按比例缩至六脉冲边界）
 *
 * @param max_linear_mod 线性调制比上限（相对 vbus，典型约 0.577）
 *
 * @param v_alpha alpha 轴电压分量。
 * @param v_beta beta 轴电压分量。
 * @param vbus_v 直流母线电压，单位 V。
 * @param out 输出的调制结果。
 */
void bm_algo_svpwm_overmod(float v_alpha,
                           float v_beta,
                           float vbus_v,
                           float max_linear_mod,
                           bm_algo_svpwm_out_t *out);

/**
 * @brief 保持方向不变地对 d/q 轴电压矢量执行圆限幅。
 * @param vd d 轴电压输入输出值。
 * @param vq q 轴电压输入输出值。
 * @param v_max 允许的电压矢量最大幅值。
 */
void bm_algo_voltage_limit(float *vd, float *vq, float v_max);

/**
 * @brief 根据 A、B 两相采样电流重构三相电流。
 * @param ia A 相采样电流。
 * @param ib B 相采样电流。
 * @param abc 三相坐标量。
 */
void bm_algo_current_from_2shunt(float ia, float ib, bm_algo_abc_t *abc);

/**
 * @brief 按兼容公式计算死区补偿后的相电压。
 * @deprecated 缺少 PWM 周期，无法进行量纲正确的补偿，仅原样返回 phase_v（空操作直通）。
 *             请改用 bm_algo_deadtime_comp_v_period()（补偿量 = sign(I)·Vbus·deadtime/period）。
 *
 * @param phase_v 补偿前的相电压，单位 V。
 * @param phase_current_a 相电流，单位 A。
 * @param deadtime_s 功率器件死区时间，单位 s。
 * @param vbus_v 直流母线电压，单位 V。
 * @return 为保持兼容性原样返回 phase_v。
 */
float bm_algo_deadtime_comp_v(float phase_v,
                              float phase_current_a,
                              float deadtime_s,
                              float vbus_v);

/**
 * @brief 结合 PWM 周期计算死区压降补偿后的相电压。
 * @param phase_v 补偿前的相电压，单位 V。
 * @param phase_current_a 相电流，单位 A。
 * @param deadtime_s 功率器件死区时间，单位 s。
 * @param pwm_period_s PWM 周期，单位 s。
 * @param vbus_v 直流母线电压，单位 V。
 * @return 返回结合 PWM 周期计算得到的补偿后相电压。
 */
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

/**
 * @brief 复位电机磁链观测器状态。
 * @param state 算法状态对象。
 * @param theta_rad 电角度，单位 rad。
 */
void bm_algo_flux_observer_reset(bm_algo_flux_observer_state_t *state,
                                 float theta_rad);

/**
 * @brief 执行一次电机磁链观测器更新。
 * 磁链观测 + PLL，返回电角度（rad）
 * 定子磁链：ψ = ∫(V - Rs·I)dt - Ls·I
 *
 * @param state 算法状态对象。
 * @param config 算法配置参数。
 * @param v_alpha alpha 轴电压分量。
 * @param v_beta beta 轴电压分量。
 * @param i_alpha alpha 轴电流分量。
 * @param i_beta beta 轴电流分量。
 * @param dt_s 本次更新的时间间隔，单位 s。
 * @return 观测得到的电角度，单位 rad；参数无效时保持并返回已有角度，state 为 NULL 时返回 0。
 */
float bm_algo_flux_observer_step(bm_algo_flux_observer_state_t *state,
                                 const bm_algo_flux_observer_config_t *config,
                                 float v_alpha,
                                 float v_beta,
                                 float i_alpha,
                                 float i_beta,
                                 float dt_s);

/**
 * @brief 根据简化 IPM 模型和 q 轴电流计算 MTPA d 轴电流参考值。
 * @param iq_ref_a q 轴电流参考值，单位 A。
 * @param ld_h d 轴电感，单位 H。
 * @param lq_h q 轴电感，单位 H。
 * @param psi_f_wb 永磁体磁链，单位 Wb。
 * @return 返回 MTPA d 轴电流参考值，单位 A。
 */
float bm_algo_mtpa_id_ref(float iq_ref_a,
                          float ld_h,
                          float lq_h,
                          float psi_f_wb);

/**
 * @brief 在电压饱和时下调弱磁 d 轴电流参考值。
 * @param id_ref_a d 轴电流参考值，单位 A。
 * @param vd d 轴电压输入输出值。
 * @param vq q 轴电压输入输出值。
 * @param v_max_pu 标幺化电压幅值上限。
 * @return 返回调整后的弱磁 d 轴电流参考值，单位 A。
 */
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

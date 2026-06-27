/**
 * @file bm_algo_power.h
 * @brief 电源纯数学核：SOGI-PLL、MPPT、RMS 与功率计量
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            SOGI 前向欧拉稳定条件文档化（ω·dt < 2）；
 *                                                bm_algo_sogi_pll_config_t 新增
 *                                                integrator_limit_ratio 字段用于积分器限幅
 * 2026-06-23       1.2            zeh            SOGI 离散由前向欧拉改为双线性（Tustin）；
 *                                                bm_algo_sogi_pll_state_t 新增
 *                                                d_alpha_prev/d_beta_prev 缓存字段
 * 2026-06-23       1.3            zeh            修复 Tustin 历史导数项系数错误，消除 SOGI 递推发散
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_POWER_H
#define BM_ALGO_POWER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- SOGI-PLL ---------- */
/**
 * @brief SOGI-PLL 配置
 *
 * @note SOGI 采用双线性（Tustin）梯形离散化，极点始终在单位圆内，
 *       对任意 dt_s > 0 均无条件稳定，适用于大步长场合。
 *
 * @note integrator_limit_ratio：PLL 积分器限幅比，限幅值 =
 *       nominal_omega_rad_s * integrator_limit_ratio。
 *       建议 0.2（即允许偏差 ±20% 额定频率），0 时使用默认值 0.2。
 */
typedef struct {
    float nominal_omega_rad_s;      /**< 额定角频率（rad/s），如 50 Hz 对应 2π×50 ≈ 314.16 */
    float k_sogi;                   /**< SOGI 带宽增益，典型值 √2 ≈ 1.414 */
    float k_pll;                    /**< PLL 比例增益，决定锁相带宽 */
    float integrator_limit_ratio;   /**< PLL 积分器限幅比（相对 nominal_omega_rad_s），
                                     *   限幅值 = nominal_omega_rad_s × ratio，
                                     *   建议 0.1~0.3，0 时自动取 0.2 */
} bm_algo_sogi_pll_config_t;

/**
 * @brief SOGI-PLL 运行状态
 *
 * @note d_alpha_prev / d_beta_prev 为 Tustin 离散所需的前一拍导数缓存，
 *       由 bm_algo_sogi_pll_reset() 清零，用户无需手动初始化。
 */
typedef struct {
    float v_alpha;       /**< SOGI 同相输出（α 轴） */
    float v_beta;        /**< SOGI 正交输出（β 轴，相位滞后 90°） */
    float theta_rad;     /**< PLL 估计相角（rad，区间 [0, 2π)） */
    float omega_rad_s;   /**< PLL 估计角频率（rad/s） */
    float integrator;    /**< PLL 积分器（频率误差累积量） */
    float d_alpha_prev;  /**< 前一拍 v_alpha 导数缓存（Tustin 梯形积分用） */
    float d_beta_prev;   /**< 前一拍 v_beta  导数缓存（Tustin 梯形积分用） */
} bm_algo_sogi_pll_state_t;

void bm_algo_sogi_pll_reset(bm_algo_sogi_pll_state_t *state,
                            const bm_algo_sogi_pll_config_t *config);
void bm_algo_sogi_pll_step(bm_algo_sogi_pll_state_t *state,
                           const bm_algo_sogi_pll_config_t *config,
                           float v_input,
                           float dt_s);

/* ---------- P&O MPPT ---------- */
typedef struct {
    float step_v;
    float v_min;
    float v_max;
} bm_algo_mppt_po_config_t;

typedef struct {
    float v_ref;
    float prev_power;
    int   direction;
} bm_algo_mppt_po_state_t;

void bm_algo_mppt_po_reset(bm_algo_mppt_po_state_t *state, float v_init);
float bm_algo_mppt_po_step(bm_algo_mppt_po_state_t *state,
                           const bm_algo_mppt_po_config_t *config,
                           float voltage,
                           float current);

/* ---------- 增量电导 MPPT ---------- */
typedef struct {
    float step_v;
    float v_min;
    float v_max;
} bm_algo_mppt_ic_config_t;

typedef struct {
    float v_ref;
    float prev_v;
    float prev_i;
} bm_algo_mppt_ic_state_t;

void bm_algo_mppt_ic_reset(bm_algo_mppt_ic_state_t *state, float v_init);
float bm_algo_mppt_ic_step(bm_algo_mppt_ic_state_t *state,
                           const bm_algo_mppt_ic_config_t *config,
                           float voltage,
                           float current);

/* ---------- RMS / 功率 ---------- */
typedef struct {
    uint32_t window_samples;
} bm_algo_rms_config_t;

typedef struct {
    float sum_sq;
    uint32_t count;
    uint32_t index;
    float *buffer;
    uint32_t buflen;
    uint32_t window_samples;
} bm_algo_rms_state_t;

int bm_algo_rms_init(bm_algo_rms_state_t *state,
                     const bm_algo_rms_config_t *config,
                     float *buffer,
                     uint32_t buflen);
void bm_algo_rms_reset(bm_algo_rms_state_t *state);
float bm_algo_rms_step(bm_algo_rms_state_t *state,
                       const bm_algo_rms_config_t *config,
                       float sample);

/** 单相瞬时有功功率；单通道 v/i 无法求无功功率，q 固定写 0。 */
void bm_algo_power_instant(float v, float i, float *p, float *q);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_POWER_H */

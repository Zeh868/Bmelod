/**
 * @file bm_algo_power.h
 * @brief 电源纯数学核：SOGI-PLL、MPPT、RMS 与功率计量
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
#ifndef BM_ALGO_POWER_H
#define BM_ALGO_POWER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- SOGI-PLL ---------- */
typedef struct {
    float nominal_omega_rad_s;
    float k_sogi;
    float k_pll;
} bm_algo_sogi_pll_config_t;

typedef struct {
    float v_alpha;
    float v_beta;
    float theta_rad;
    float omega_rad_s;
    float integrator;
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

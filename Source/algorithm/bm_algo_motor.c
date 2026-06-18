/**
 * @file bm_algo_motor.c
 * @brief 电机数学核：Clarke/Park 与 SVPWM 实现
 *
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
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/algorithm/bm_algo_common.h"
#include <stddef.h>

#include <math.h>

#ifndef BM_ALGO_SQRT3_F
#define BM_ALGO_SQRT3_F 1.7320508075688772f
#endif

void bm_algo_clarke(const bm_algo_abc_t *abc, bm_algo_alphabeta_t *ab) {
    if (abc == NULL || ab == NULL) {
        return;
    }
    ab->i_alpha = abc->ia;
    ab->i_beta = (abc->ia + 2.0f * abc->ib) / BM_ALGO_SQRT3_F;
}

void bm_algo_clarke_2shunt(float ia, float ib, bm_algo_alphabeta_t *ab) {
    if (ab == NULL) {
        return;
    }
    ab->i_alpha = ia;
    ab->i_beta = (ia + 2.0f * ib) / BM_ALGO_SQRT3_F;
}

void bm_algo_park(const bm_algo_alphabeta_t *ab,
                  float theta_rad,
                  bm_algo_dq_t *dq) {
    float c;
    float s;

    if (ab == NULL || dq == NULL) {
        return;
    }

    c = cosf(theta_rad);
    s = sinf(theta_rad);
    dq->id =  c * ab->i_alpha + s * ab->i_beta;
    dq->iq = -s * ab->i_alpha + c * ab->i_beta;
}

void bm_algo_inv_park(const bm_algo_dq_t *dq,
                      float theta_rad,
                      bm_algo_alphabeta_t *ab) {
    float c;
    float s;

    if (dq == NULL || ab == NULL) {
        return;
    }

    c = cosf(theta_rad);
    s = sinf(theta_rad);
    ab->i_alpha = c * dq->id - s * dq->iq;
    ab->i_beta  = s * dq->id + c * dq->iq;
}

void bm_algo_inv_clarke(const bm_algo_alphabeta_t *ab, bm_algo_abc_t *abc) {
    if (ab == NULL || abc == NULL) {
        return;
    }

    abc->ia = ab->i_alpha;
    abc->ib = -0.5f * ab->i_alpha + 0.5f * BM_ALGO_SQRT3_F * ab->i_beta;
    abc->ic = -0.5f * ab->i_alpha - 0.5f * BM_ALGO_SQRT3_F * ab->i_beta;
}

void bm_algo_voltage_limit(float *vd, float *vq, float v_max) {
    float mag;

    if (vd == NULL || vq == NULL || v_max <= 0.0f) {
        return;
    }

    mag = sqrtf((*vd) * (*vd) + (*vq) * (*vq));
    if (mag > v_max) {
        float scale = v_max / mag;
        *vd *= scale;
        *vq *= scale;
    }
}

void bm_algo_svpwm(float v_alpha,
                   float v_beta,
                   float vbus_v,
                   bm_algo_svpwm_out_t *out) {
    float va;
    float vb;
    float vc;
    float v_offset;
    float v_max;
    float v_min;
    float inv_vbus;

    if (out == NULL) {
        return;
    }

    if (!bm_algo_is_finite_f(v_alpha) ||
        !bm_algo_is_finite_f(v_beta) ||
        !bm_algo_is_finite_f(vbus_v) ||
        vbus_v <= 0.0f) {
        out->duty_a = 0.5f;
        out->duty_b = 0.5f;
        out->duty_c = 0.5f;
        return;
    }

    inv_vbus = 1.0f / vbus_v;

    /* 逆 Clarke 得相电压 */
    va = v_alpha;
    vb = -0.5f * v_alpha + 0.5f * BM_ALGO_SQRT3_F * v_beta;
    vc = -0.5f * v_alpha - 0.5f * BM_ALGO_SQRT3_F * v_beta;

    /* 中点注入（min-max 法） */
    v_max = va;
    if (vb > v_max) {
        v_max = vb;
    }
    if (vc > v_max) {
        v_max = vc;
    }

    v_min = va;
    if (vb < v_min) {
        v_min = vb;
    }
    if (vc < v_min) {
        v_min = vc;
    }

    v_offset = -0.5f * (v_max + v_min);
    va += v_offset;
    vb += v_offset;
    vc += v_offset;

    out->duty_a = bm_algo_clamp_f(0.5f + va * inv_vbus, 0.0f, 1.0f);
    out->duty_b = bm_algo_clamp_f(0.5f + vb * inv_vbus, 0.0f, 1.0f);
    out->duty_c = bm_algo_clamp_f(0.5f + vc * inv_vbus, 0.0f, 1.0f);
}

void bm_algo_svpwm_overmod(float v_alpha,
                           float v_beta,
                           float vbus_v,
                           float max_linear_mod,
                           bm_algo_svpwm_out_t *out) {
    float mag;
    float limit_v;
    float scale;

    if (out == NULL) {
        return;
    }

    if (max_linear_mod <= 0.0f || vbus_v <= 0.0f) {
        bm_algo_svpwm(v_alpha, v_beta, vbus_v, out);
        return;
    }

    mag = sqrtf(v_alpha * v_alpha + v_beta * v_beta);
    limit_v = max_linear_mod * vbus_v;
    if (mag <= limit_v || mag <= 0.0f) {
        bm_algo_svpwm(v_alpha, v_beta, vbus_v, out);
        return;
    }

    scale = limit_v / mag;
    bm_algo_svpwm(v_alpha * scale, v_beta * scale, vbus_v, out);
}

void bm_algo_current_from_2shunt(float ia, float ib, bm_algo_abc_t *abc) {
    if (abc == NULL) {
        return;
    }
    abc->ia = ia;
    abc->ib = ib;
    abc->ic = -ia - ib;
}

float bm_algo_deadtime_comp_v(float phase_v,
                              float phase_current_a,
                              float deadtime_s,
                              float vbus_v) {
    (void)phase_current_a;
    (void)deadtime_s;
    (void)vbus_v;
    return phase_v;
}

float bm_algo_deadtime_comp_v_period(float phase_v,
                                     float phase_current_a,
                                     float deadtime_s,
                                     float pwm_period_s,
                                     float vbus_v) {
    float sign_i;
    float comp;

    if (!bm_algo_is_finite_f(phase_v) ||
        !bm_algo_is_finite_f(phase_current_a) ||
        !bm_algo_is_finite_f(deadtime_s) ||
        !bm_algo_is_finite_f(pwm_period_s) ||
        !bm_algo_is_finite_f(vbus_v) ||
        deadtime_s <= 0.0f || pwm_period_s <= 0.0f ||
        deadtime_s >= pwm_period_s || vbus_v <= 0.0f) {
        return phase_v;
    }

    if (phase_current_a > 0.0f) {
        sign_i = 1.0f;
    } else if (phase_current_a < 0.0f) {
        sign_i = -1.0f;
    } else {
        return phase_v;
    }

    comp = sign_i * (deadtime_s / pwm_period_s) * vbus_v;
    return phase_v + comp;
}

void bm_algo_flux_observer_reset(bm_algo_flux_observer_state_t *state,
                                 float theta_rad) {
    if (state != NULL) {
        state->theta_rad = theta_rad;
        state->omega_rad_s = 0.0f;
        state->flux_alpha = 0.0f;
        state->flux_beta = 0.0f;
    }
}

float bm_algo_flux_observer_step(bm_algo_flux_observer_state_t *state,
                                 const bm_algo_flux_observer_config_t *config,
                                 float v_alpha,
                                 float v_beta,
                                 float i_alpha,
                                 float i_beta,
                                 float dt_s) {
    float v_alpha_emf;
    float v_beta_emf;
    float flux_alpha;
    float flux_beta;
    float theta_meas;
    float pll_error;

    if (state == NULL || config == NULL || dt_s <= 0.0f) {
        return 0.0f;
    }

    v_alpha_emf = v_alpha - config->rs_ohm * i_alpha;
    v_beta_emf = v_beta - config->rs_ohm * i_beta;
    state->flux_alpha += v_alpha_emf * dt_s;
    state->flux_beta += v_beta_emf * dt_s;

    flux_alpha = state->flux_alpha - config->ls_h * i_alpha;
    flux_beta = state->flux_beta - config->ls_h * i_beta;
    theta_meas = atan2f(flux_beta, flux_alpha);
    pll_error = bm_algo_angle_delta_rad(state->theta_rad, theta_meas);

    state->omega_rad_s += config->pll_ki * pll_error * dt_s;
    state->theta_rad += state->omega_rad_s * dt_s + config->pll_kp * pll_error;
    state->theta_rad = bm_algo_angle_wrap_rad(state->theta_rad);
    return state->theta_rad;
}

float bm_algo_mtpa_id_ref(float iq_ref_a,
                          float ld_h,
                          float lq_h,
                          float psi_f_wb) {
    float delta_l;
    float root;

    delta_l = lq_h - ld_h;
    if (fabsf(delta_l) < 1e-9f || ld_h <= 0.0f || lq_h <= 0.0f ||
        psi_f_wb < 0.0f || fabsf(iq_ref_a) < 1e-9f) {
        return 0.0f;
    }

    root = sqrtf(psi_f_wb * psi_f_wb
                 + 8.0f * delta_l * delta_l * iq_ref_a * iq_ref_a);
    return -2.0f * delta_l * iq_ref_a * iq_ref_a /
           (psi_f_wb + root);
}

float bm_algo_fw_id_adjust(float id_ref_a, float vd, float vq, float v_max_pu) {
    float v_mag;

    if (v_max_pu <= 0.0f) {
        return id_ref_a;
    }
    v_mag = sqrtf(vd * vd + vq * vq);
    if (v_mag <= v_max_pu) {
        return id_ref_a;
    }
    return id_ref_a - 0.2f * (v_mag - v_max_pu);
}

static float norm_deg_f(float deg) {
    while (deg < 0.0f) {
        deg += 360.0f;
    }
    while (deg >= 360.0f) {
        deg -= 360.0f;
    }
    return deg;
}

static float ang_diff_deg(float a, float b) {
    float d = fabsf(a - b);
    if (d > 180.0f) {
        d = 360.0f - d;
    }
    return d;
}

int bm_algo_pwm_sample_window_valid(uint32_t sector,
                                    float adc_phase_deg,
                                    float window_deg) {
    float sector_center;
    float phase;

    if (sector > 5u || window_deg <= 0.0f) {
        return 0;
    }

    phase = norm_deg_f(adc_phase_deg);
    sector_center = (float)sector * 60.0f + 30.0f;
    return (ang_diff_deg(phase, sector_center) <= window_deg) ? 1 : 0;
}

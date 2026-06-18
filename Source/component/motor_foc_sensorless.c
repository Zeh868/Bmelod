/**
 * @file motor_foc_sensorless.c
 * @brief 无感 FOC 领域组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            启动状态机
 *
 */
#include "bm/component/motor_foc_sensorless.h"

#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static float adc_to_current(float scale, uint16_t raw) {
    return ((float)((int32_t)raw - 32768)) / scale;
}

static int sim_feedback_active(const bm_motor_foc_sensorless_resources_t *res) {
    return res->sim_fb.id_a != NULL &&
           res->sim_fb.iq_a != NULL &&
           res->sim_fb.theta_elec_rad != NULL;
}

static float cfg_align_time_s(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->align_time_s > 0.0f) ? cfg->align_time_s : 0.2f;
}

static float cfg_open_loop_ramp_s(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->open_loop_ramp_s > 0.0f) ? cfg->open_loop_ramp_s : 0.5f;
}

static float cfg_omega_start(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->open_loop_omega_start > 0.0f) ? cfg->open_loop_omega_start : 20.0f;
}

static float cfg_omega_end(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->open_loop_omega_end > 0.0f) ? cfg->open_loop_omega_end : 200.0f;
}

static float cfg_align_id_a(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->align_id_a > 0.0f) ? cfg->align_id_a : 0.3f;
}

static float cfg_lock_omega(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->observer_lock_omega_rad_s > 0.0f) ?
           cfg->observer_lock_omega_rad_s : 30.0f;
}

static float cfg_lock_time_s(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->observer_lock_time_s > 0.0f) ? cfg->observer_lock_time_s : 0.5f;
}

static void pwm_zero(const bm_motor_foc_sensorless_resources_t *res) {
    if (res->pwm != NULL) {
        (void)bm_hal_pwm_set_duty(res->pwm, 0u, 0u);
        (void)bm_hal_pwm_set_duty(res->pwm, 1u, 0u);
        (void)bm_hal_pwm_set_duty(res->pwm, 2u, 0u);
    }
}

static void latch_fault(bm_motor_foc_sensorless_axis_t *axis) {
    bm_motor_foc_sensorless_state_t *st = &axis->state;

    st->phase = BM_MOTOR_SL_PHASE_FAULT;
    if (!st->fault_latched) {
        st->fault_latched = 1;
        st->fault_count++;
    }
    bm_algo_pi_reset(&st->pi_d, 0.0f);
    bm_algo_pi_reset(&st->pi_q, 0.0f);
    pwm_zero(&axis->resources);
    if (axis->resources.pwm != NULL) {
        bm_hal_pwm_request_safe_state(axis->resources.pwm);
    }
}

static void sync_command(bm_motor_foc_sensorless_axis_t *axis) {
    bm_motor_sl_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_motor_foc_sensorless_apply_command(axis, &command);
    }
}

static int read_current_ab(const bm_motor_foc_sensorless_axis_t *axis,
                           float *ia,
                           float *ib) {
    const bm_motor_foc_sensorless_resources_t *res = &axis->resources;
    uint16_t raw_ia = 0u;
    uint16_t raw_ib = 0u;

    if (sim_feedback_active(res)) {
        return -1;
    }
    if (res->adc == NULL || res->current_adc_scale <= 0.0f) {
        return -1;
    }
    if (bm_hal_adc_read_injected(res->adc, res->adc_rank_ia, &raw_ia) != BM_OK) {
        return -1;
    }
    if (bm_hal_adc_read_injected(res->adc, res->adc_rank_ib, &raw_ib) != BM_OK) {
        return -1;
    }
    *ia = adc_to_current(res->current_adc_scale, raw_ia);
    *ib = adc_to_current(res->current_adc_scale, raw_ib);
    return 0;
}

static int read_id_iq(const bm_motor_foc_sensorless_axis_t *axis,
                      float theta_elec,
                      float ia,
                      float ib,
                      float *id,
                      float *iq) {
    const bm_motor_foc_sensorless_resources_t *res = &axis->resources;
    bm_algo_alphabeta_t i_ab;
    bm_algo_dq_t      i_dq;

    if (res->sim_fb.id_a != NULL && res->sim_fb.iq_a != NULL) {
        *id = *res->sim_fb.id_a;
        *iq = *res->sim_fb.iq_a;
        return 0;
    }
    bm_algo_clarke_2shunt(ia, ib, &i_ab);
    bm_algo_park(&i_ab, theta_elec, &i_dq);
    *id = i_dq.id;
    *iq = i_dq.iq;
    return 0;
}

static void begin_enable(bm_motor_foc_sensorless_axis_t *axis) {
    bm_motor_foc_sensorless_state_t *st = &axis->state;
    const bm_motor_foc_sensorless_config_t *cfg = &axis->config;

    st->phase = BM_MOTOR_SL_PHASE_ALIGN;
    st->phase_timer_s = 0.0f;
    st->open_loop_theta = 0.0f;
    st->open_loop_omega = cfg_omega_start(cfg);
    st->lock_loss_timer_s = 0.0f;
    bm_algo_flux_observer_reset(&st->observer, 0.0f);
}

int bm_motor_foc_sensorless_validate_config(
    const bm_motor_foc_sensorless_config_t *config) {
    if (config == NULL || config->vbus_v <= 0.0f ||
        config->phase_r_ohm <= 0.0f || config->current_dt_s <= 0.0f ||
        config->pole_pairs <= 0.0f || config->iq_max_a <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->observer.rs_ohm <= 0.0f || config->observer.ls_h <= 0.0f ||
        config->observer.pll_kp < 0.0f || config->observer.pll_ki < 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->enable_mtpa &&
        (config->ld_h <= 0.0f || config->lq_h <= 0.0f)) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_motor_foc_sensorless_reset(bm_motor_foc_sensorless_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_pi_reset(&axis->state.pi_d, 0.0f);
    bm_algo_pi_reset(&axis->state.pi_q, 0.0f);
    bm_algo_flux_observer_reset(&axis->state.observer, 0.0f);
    axis->state.phase = BM_MOTOR_SL_PHASE_IDLE;
    axis->state.phase_timer_s = 0.0f;
    axis->state.open_loop_theta = 0.0f;
    axis->state.open_loop_omega = 0.0f;
    axis->state.lock_loss_timer_s = 0.0f;
    axis->state.last_vd_pu = 0.0f;
    axis->state.last_vq_pu = 0.0f;
    axis->state.loop_count = 0u;
    axis->state.fault_latched = 0;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

void bm_motor_foc_sensorless_apply_command(bm_motor_foc_sensorless_axis_t *axis,
                                           const bm_motor_sl_cmd_t *cmd) {
    if (axis == NULL || cmd == NULL) {
        return;
    }

    axis->state.cmd = *cmd;
    if ((cmd->status & BM_MOTOR_SL_CMD_FAULT) != 0u) {
        latch_fault(axis);
        return;
    }

    if ((cmd->status & BM_MOTOR_SL_CMD_ENABLED) != 0u &&
        axis->state.phase == BM_MOTOR_SL_PHASE_IDLE &&
        !axis->state.fault_latched) {
        begin_enable(axis);
    }
}

void bm_motor_foc_sensorless_current_step(bm_motor_foc_sensorless_axis_t *axis) {
    const bm_motor_foc_sensorless_config_t *cfg;
    bm_motor_foc_sensorless_resources_t *res;
    bm_motor_foc_sensorless_state_t *st;
    bm_motor_sl_telemetry_t *tel;
    float theta_elec;
    float ia = 0.0f;
    float ib = 0.0f;
    float id_meas;
    float iq_meas;
    float iq_ref;
    float id_ref;
    float vd;
    float vq;
    float ramp_t;
    bm_algo_dq_t v_dq;
    bm_algo_alphabeta_t v_ab;
    bm_algo_alphabeta_t i_ab;
    bm_algo_svpwm_out_t svpwm;
    int saturated;
    int use_sim;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    res = &axis->resources;
    st = &axis->state;
    tel = &st->telemetry;
    use_sim = sim_feedback_active(res);

    if (st->fault_latched ||
        (st->cmd.status & BM_MOTOR_SL_CMD_FAULT) != 0u ||
        st->phase == BM_MOTOR_SL_PHASE_FAULT) {
        latch_fault(axis);
        tel->status = BM_MOTOR_SL_TEL_FAULT;
        tel->phase = BM_MOTOR_SL_PHASE_FAULT;
        tel->sequence = st->loop_count;
        st->loop_count++;
        return;
    }

    if ((st->cmd.status & BM_MOTOR_SL_CMD_ENABLED) == 0u) {
        if (st->phase != BM_MOTOR_SL_PHASE_IDLE) {
            st->phase = BM_MOTOR_SL_PHASE_IDLE;
            st->phase_timer_s = 0.0f;
            bm_algo_pi_reset(&st->pi_d, 0.0f);
            bm_algo_pi_reset(&st->pi_q, 0.0f);
        }
        pwm_zero(res);
        st->loop_count++;
        return;
    }

    if (st->phase == BM_MOTOR_SL_PHASE_IDLE) {
        begin_enable(axis);
    }

    st->phase_timer_s += cfg->current_dt_s;

    switch (st->phase) {
    case BM_MOTOR_SL_PHASE_ALIGN:
        theta_elec = 0.0f;
        id_ref = cfg_align_id_a(cfg);
        iq_ref = 0.0f;
        if (st->phase_timer_s >= cfg_align_time_s(cfg)) {
            st->phase = BM_MOTOR_SL_PHASE_OPEN_LOOP;
            st->phase_timer_s = 0.0f;
            st->open_loop_theta = 0.0f;
            st->open_loop_omega = cfg_omega_start(cfg);
        }
        break;

    case BM_MOTOR_SL_PHASE_OPEN_LOOP:
        ramp_t = st->phase_timer_s / cfg_open_loop_ramp_s(cfg);
        if (ramp_t > 1.0f) {
            ramp_t = 1.0f;
        }
        st->open_loop_omega = cfg_omega_start(cfg) +
            (cfg_omega_end(cfg) - cfg_omega_start(cfg)) * ramp_t;
        st->open_loop_theta = bm_algo_angle_wrap_rad(
            st->open_loop_theta + st->open_loop_omega * cfg->current_dt_s);
        theta_elec = st->open_loop_theta;
        id_ref = 0.0f;
        iq_ref = bm_algo_clamp_f(st->cmd.iq_ref_a, -cfg->iq_max_a, cfg->iq_max_a);
        if (st->phase_timer_s >= cfg_open_loop_ramp_s(cfg)) {
            st->phase = BM_MOTOR_SL_PHASE_OBSERVER;
            st->phase_timer_s = 0.0f;
            st->lock_loss_timer_s = 0.0f;
            bm_algo_flux_observer_reset(&st->observer, theta_elec);
        }
        break;

    case BM_MOTOR_SL_PHASE_OBSERVER:
    default:
        if (!use_sim) {
            if (read_current_ab(axis, &ia, &ib) != 0) {
                latch_fault(axis);
                tel->status = BM_MOTOR_SL_TEL_FAULT;
                tel->phase = BM_MOTOR_SL_PHASE_FAULT;
                st->loop_count++;
                return;
            }

            theta_elec = st->observer.theta_rad;
            v_dq.id = st->last_vd_pu;
            v_dq.iq = st->last_vq_pu;
            bm_algo_inv_park(&v_dq, theta_elec, &v_ab);
            bm_algo_clarke_2shunt(ia, ib, &i_ab);
            (void)bm_algo_flux_observer_step(&st->observer, &cfg->observer,
                                             v_ab.i_alpha * cfg->vbus_v,
                                             v_ab.i_beta * cfg->vbus_v,
                                             i_ab.i_alpha, i_ab.i_beta,
                                             cfg->current_dt_s);
            theta_elec = st->observer.theta_rad;

            if (fabsf(st->observer.omega_rad_s) < cfg_lock_omega(cfg)) {
                st->lock_loss_timer_s += cfg->current_dt_s;
            } else {
                st->lock_loss_timer_s = 0.0f;
            }
            if (st->lock_loss_timer_s >= cfg_lock_time_s(cfg)) {
                latch_fault(axis);
                tel->status = BM_MOTOR_SL_TEL_FAULT;
                tel->phase = BM_MOTOR_SL_PHASE_FAULT;
                st->loop_count++;
                return;
            }
        } else {
            theta_elec = bm_algo_angle_wrap_rad(*res->sim_fb.theta_elec_rad);
        }

        iq_ref = bm_algo_clamp_f(st->cmd.iq_ref_a, -cfg->iq_max_a, cfg->iq_max_a);
        id_ref = 0.0f;
        if (cfg->enable_mtpa) {
            id_ref = bm_algo_mtpa_id_ref(iq_ref, cfg->ld_h, cfg->lq_h, cfg->psi_f_wb);
        }
        break;
    }

    if (read_id_iq(axis, theta_elec, ia, ib, &id_meas, &iq_meas) != 0) {
        latch_fault(axis);
        tel->status = BM_MOTOR_SL_TEL_FAULT;
        tel->phase = BM_MOTOR_SL_PHASE_FAULT;
        st->loop_count++;
        return;
    }

    if (cfg->enable_fw && st->phase == BM_MOTOR_SL_PHASE_OBSERVER) {
        id_ref = bm_algo_fw_id_adjust(id_ref, st->last_vd_pu, st->last_vq_pu,
                                      cfg->v_max_pu);
    }

    vd = bm_algo_pi_step(&st->pi_d, &cfg->pi_d, id_ref - id_meas,
                         cfg->current_dt_s);
    vq = bm_algo_pi_step(&st->pi_q, &cfg->pi_q, iq_ref - iq_meas,
                         cfg->current_dt_s);
    bm_algo_voltage_limit(&vd, &vq, cfg->v_max_pu);
    st->last_vd_pu = vd;
    st->last_vq_pu = vq;

    saturated = (fabsf(vd) >= cfg->v_max_pu - 1e-4f) ||
                (fabsf(vq) >= cfg->v_max_pu - 1e-4f);

    v_dq.id = vd;
    v_dq.iq = vq;
    bm_algo_inv_park(&v_dq, theta_elec, &v_ab);
    bm_algo_svpwm(v_ab.i_alpha * 0.5f * cfg->vbus_v,
                  v_ab.i_beta * 0.5f * cfg->vbus_v,
                  cfg->vbus_v,
                  &svpwm);

    if (res->pwm != NULL && res->pwm_max > 0u) {
        (void)bm_hal_pwm_set_duty(res->pwm, 0u,
            (uint16_t)(svpwm.duty_a * (float)res->pwm_max));
        (void)bm_hal_pwm_set_duty(res->pwm, 1u,
            (uint16_t)(svpwm.duty_b * (float)res->pwm_max));
        (void)bm_hal_pwm_set_duty(res->pwm, 2u,
            (uint16_t)(svpwm.duty_c * (float)res->pwm_max));
    }
    if (res->on_voltage != NULL) {
        res->on_voltage(res->on_voltage_user, vd, vq, theta_elec);
    }

    tel->sequence = st->loop_count;
    tel->status = BM_MOTOR_SL_TEL_VALID;
    if (saturated) {
        tel->status |= BM_MOTOR_SL_TEL_SAT;
    }
    tel->id_meas_a = id_meas;
    tel->iq_meas_a = iq_meas;
    tel->theta_elec_rad = theta_elec;
    tel->omega_rad_s = use_sim ? st->open_loop_omega : st->observer.omega_rad_s;
    tel->iq_ref_a = iq_ref;
    tel->phase = st->phase;
    st->loop_count++;
}

void bm_motor_foc_sensorless_exec_current(const bm_exec_t *instance) {
    bm_motor_foc_sensorless_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (bm_motor_foc_sensorless_axis_t *)instance->state;
    sync_command(axis);
    bm_motor_foc_sensorless_current_step(axis);
    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user,
            &axis->state.telemetry);
    }
}

int bm_motor_foc_sensorless_exec_init(const bm_exec_t *instance) {
    bm_motor_foc_sensorless_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_motor_foc_sensorless_axis_t *)instance->state;
    if (bm_motor_foc_sensorless_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_motor_foc_sensorless_reset(axis);
    return BM_OK;
}

int bm_motor_foc_sensorless_exec_start(const bm_exec_t *instance) {
    const bm_motor_foc_sensorless_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (const bm_motor_foc_sensorless_axis_t *)instance->state;
    if (axis->resources.pwm == NULL) {
        return BM_OK;
    }
    return bm_hal_pwm_enable_outputs(axis->resources.pwm, 1);
}

void bm_motor_foc_sensorless_exec_safe_stop(const bm_exec_t *instance) {
    const bm_motor_foc_sensorless_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (const bm_motor_foc_sensorless_axis_t *)instance->state;
    if (axis->resources.pwm != NULL) {
        bm_hal_pwm_request_safe_state(axis->resources.pwm);
    }
}

const bm_exec_ops_t bm_motor_foc_sensorless_exec_ops = {
    bm_motor_foc_sensorless_exec_init,
    bm_motor_foc_sensorless_exec_start,
    bm_motor_foc_sensorless_exec_safe_stop
};

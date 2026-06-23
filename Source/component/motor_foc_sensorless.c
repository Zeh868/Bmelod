/**
 * @file motor_foc_sensorless.c
 * @brief 无感 FOC 领域组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            启动状态机
 * 2026-06-23       0.3            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/component/motor_foc_sensorless.h"

#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/**
 * @brief 将 ADC 原始值转换为相电流（静态辅助）
 *
 * 以 raw=32768 对应零电流，满量程由 scale 决定（A/LSB 的倒数）。
 *
 * @param scale ADC 满码对应电流（A）；须 > 0
 * @param raw   ADC 原始采样值（无符号 16 位）
 * @return 对应相电流（A），正负号由原始值相对 32768 的偏差决定
 */
static float adc_to_current(float scale, uint16_t raw) {
    return ((float)((int32_t)raw - 32768)) / scale;
}

/**
 * @brief 判断仿真反馈是否全部激活（静态辅助）
 *
 * 三路指针（id_a / iq_a / theta_elec_rad）均非 NULL 时返回真。
 *
 * @param res 资源结构体指针
 * @return 非零：仿真反馈有效；0：使用真实 ADC
 */
static int sim_feedback_active(const bm_motor_foc_sensorless_resources_t *res) {
    return res->sim_fb.id_a != NULL &&
           res->sim_fb.iq_a != NULL &&
           res->sim_fb.theta_elec_rad != NULL;
}

/**
 * @brief 获取预对齐持续时间（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return align_time_s（> 0）；配置值 ≤ 0 时返回 0.2 s
 */
static float cfg_align_time_s(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->align_time_s > 0.0f) ? cfg->align_time_s : 0.2f;
}

/**
 * @brief 获取开环升速斜坡时间（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return open_loop_ramp_s（> 0）；配置值 ≤ 0 时返回 0.5 s
 */
static float cfg_open_loop_ramp_s(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->open_loop_ramp_s > 0.0f) ? cfg->open_loop_ramp_s : 0.5f;
}

/**
 * @brief 获取开环初始角速度（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return open_loop_omega_start（> 0）；配置值 ≤ 0 时返回 20 rad/s
 */
static float cfg_omega_start(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->open_loop_omega_start > 0.0f) ? cfg->open_loop_omega_start : 20.0f;
}

/**
 * @brief 获取开环目标角速度（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return open_loop_omega_end（> 0）；配置值 ≤ 0 时返回 200 rad/s
 */
static float cfg_omega_end(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->open_loop_omega_end > 0.0f) ? cfg->open_loop_omega_end : 200.0f;
}

/**
 * @brief 获取预对齐注入 d 轴电流（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return align_id_a（> 0）；配置值 ≤ 0 时返回 0.3 A
 */
static float cfg_align_id_a(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->align_id_a > 0.0f) ? cfg->align_id_a : 0.3f;
}

/**
 * @brief 获取观测器锁定判定最小角速度（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return observer_lock_omega_rad_s（> 0）；配置值 ≤ 0 时返回 30 rad/s
 */
static float cfg_lock_omega(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->observer_lock_omega_rad_s > 0.0f) ?
           cfg->observer_lock_omega_rad_s : 30.0f;
}

/**
 * @brief 获取观测器锁定故障触发时间（静态辅助，带默认值保护）
 *
 * @param cfg 配置指针
 * @return observer_lock_time_s（> 0）；配置值 ≤ 0 时返回 0.5 s
 */
static float cfg_lock_time_s(const bm_motor_foc_sensorless_config_t *cfg) {
    return (cfg->observer_lock_time_s > 0.0f) ? cfg->observer_lock_time_s : 0.5f;
}

/**
 * @brief 将三相 PWM 占空比全置零（静态辅助）
 *
 * resources.pwm 为 NULL 时静默跳过。
 *
 * @param res 资源结构体指针
 */
static void pwm_zero(const bm_motor_foc_sensorless_resources_t *res) {
    if (res->pwm != NULL) {
        (void)bm_hal_pwm_set_duty(res->pwm, 0u, 0u);
        (void)bm_hal_pwm_set_duty(res->pwm, 1u, 0u);
        (void)bm_hal_pwm_set_duty(res->pwm, 2u, 0u);
    }
}

/**
 * @brief 锁存故障并将 PWM 置安全状态（静态辅助）
 *
 * 置相位为 FAULT，递增 fault_count，复位两轴 PI，
 * 调用 pwm_zero() 后向 HAL 发出安全状态请求。
 *
 * @param axis 实例指针
 */
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

/**
 * @brief 从回调读取最新命令并应用（静态辅助）
 *
 * read_command 回调返回 0 时调用 bm_motor_foc_sensorless_apply_command()；
 * 回调为 NULL 或返回非零时保持旧命令。
 *
 * @param axis 实例指针
 */
static void sync_command(bm_motor_foc_sensorless_axis_t *axis) {
    bm_motor_sl_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(axis->resources.read_command_user,
                                     &command) == 0) {
        bm_motor_foc_sensorless_apply_command(axis, &command);
    }
}

/**
 * @brief 从 ADC 读取 A/B 相电流（静态辅助）
 *
 * 仿真反馈激活时直接返回 -1 通知调用方使用仿真值；
 * ADC 句柄为 NULL 或 scale ≤ 0 时同样返回 -1。
 *
 * @param axis 实例指针（只读）
 * @param ia   输出：A 相电流（A）
 * @param ib   输出：B 相电流（A）
 * @return 0 成功；-1 应使用仿真反馈或 ADC 不可用
 */
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

/**
 * @brief 计算 d/q 轴电流（静态辅助）
 *
 * 仿真反馈激活时直接读取 sim_fb 中的 id/iq；
 * 否则对 ia/ib 执行 Clarke（两分流） + Park 变换。
 *
 * @param axis       实例指针（只读）
 * @param theta_elec 当前电气角（rad）
 * @param ia         A 相电流（A），仿真模式下忽略
 * @param ib         B 相电流（A），仿真模式下忽略
 * @param id         输出：d 轴电流（A）
 * @param iq         输出：q 轴电流（A）
 * @return 始终返回 0
 */
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

/**
 * @brief 进入使能序列：置相位为 ALIGN 并初始化开环参数（静态辅助）
 *
 * 复位磁链观测器，设置开环初始 omega 与计时器归零。
 *
 * @param axis 实例指针
 */
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

/**
 * @brief exec 封装：同步命令后执行一步电流环并发布遥测
 *
 * 先调用 sync_command() 拉取最新命令，再调用
 * bm_motor_foc_sensorless_current_step()，最后通过
 * publish_telemetry 回调发布遥测。
 *
 * @param instance exec 实例指针，instance->state 须为 bm_motor_foc_sensorless_axis_t*
 */
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

/**
 * @brief exec 生命周期：初始化（校验配置并复位状态）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
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

/**
 * @brief exec 生命周期：启动（使能 PWM 输出桥臂）
 *
 * 若 resources.pwm 非 NULL 则调用 bm_hal_pwm_enable_outputs()。
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功或无 PWM；BM_ERR_INVALID 指针为空
 */
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

/**
 * @brief exec 生命周期：安全停机（触发 PWM 安全状态）
 *
 * 调用 bm_hal_pwm_request_safe_state() 将桥臂置于安全关断/制动状态。
 *
 * @param instance exec 实例指针
 */
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

/**
 * @brief motor_foc_sensorless 标准 exec ops 表
 *
 * 将此指针赋给 bm_exec_t::ops，即可将 motor_foc_sensorless 实例
 * 接入调度框架的生命周期管理。
 */
const bm_exec_ops_t bm_motor_foc_sensorless_exec_ops = {
    bm_motor_foc_sensorless_exec_init,
    bm_motor_foc_sensorless_exec_start,
    bm_motor_foc_sensorless_exec_safe_stop
};

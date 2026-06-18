/**
 * @file motor_foc_sensorless.h
 * @brief 无感 FOC 领域组件（启动状态机 + 磁链观测 + MTPA/弱磁 + 电流环）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            增加启动状态机
 *
 * 电流环使用磁链观测器角度；命令 iq_ref_a 为 q 轴电流参考（A）。
 */
#ifndef BM_MOTOR_FOC_SENSORLESS_H
#define BM_MOTOR_FOC_SENSORLESS_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/hybrid/bm_exec.h"
#include "hal/bm_hal_adc.h"
#include "hal/bm_hal_pwm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_MOTOR_SL_CMD_ENABLED  (1u << 0u)
#define BM_MOTOR_SL_CMD_FAULT    (1u << 1u)

#define BM_MOTOR_SL_TEL_VALID    (1u << 0u)
#define BM_MOTOR_SL_TEL_SAT      (1u << 1u)
#define BM_MOTOR_SL_TEL_FAULT    (1u << 2u)

typedef enum {
    BM_MOTOR_SL_PHASE_IDLE = 0,
    BM_MOTOR_SL_PHASE_ALIGN,
    BM_MOTOR_SL_PHASE_OPEN_LOOP,
    BM_MOTOR_SL_PHASE_OBSERVER,
    BM_MOTOR_SL_PHASE_FAULT
} bm_motor_sl_phase_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    iq_ref_a; /**< q 轴电流参考（A） */
} bm_motor_sl_cmd_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    id_meas_a;
    float    iq_meas_a;
    float    theta_elec_rad;
    float    omega_rad_s;
    float    iq_ref_a;
    bm_motor_sl_phase_t phase;
} bm_motor_sl_telemetry_t;

typedef struct {
    float *id_a;
    float *iq_a;
    float *theta_elec_rad;
} bm_motor_sl_sim_feedback_t;

typedef void (*bm_motor_sl_on_voltage_fn)(void *user,
                                          float vd_pu,
                                          float vq_pu,
                                          float theta_elec_rad);

typedef int (*bm_motor_sl_read_command_fn)(void *user,
                                           bm_motor_sl_cmd_t *command);

typedef void (*bm_motor_sl_publish_telemetry_fn)(
    void *user,
    const bm_motor_sl_telemetry_t *telemetry);

typedef struct {
    const bm_hal_adc_t *adc;
    const bm_hal_pwm_t *pwm;
    uint32_t adc_rank_ia;
    uint32_t adc_rank_ib;
    uint16_t pwm_max;
    float    current_adc_scale;
    bm_motor_sl_sim_feedback_t sim_fb;
    bm_motor_sl_on_voltage_fn on_voltage;
    void    *on_voltage_user;
    bm_motor_sl_read_command_fn read_command;
    void    *read_command_user;
    bm_motor_sl_publish_telemetry_fn publish_telemetry;
    void    *publish_telemetry_user;
} bm_motor_foc_sensorless_resources_t;

typedef struct {
    float    pole_pairs;
    float    vbus_v;
    float    phase_r_ohm;
    float    ld_h;
    float    lq_h;
    float    psi_f_wb;
    float    v_max_pu;
    float    current_dt_s;
    float    iq_max_a;
    int      enable_mtpa;
    int      enable_fw;
    float    align_time_s;
    float    align_id_a;
    float    open_loop_omega_start;
    float    open_loop_omega_end;
    float    open_loop_ramp_s;
    float    observer_lock_omega_rad_s;
    float    observer_lock_time_s;
    bm_algo_pi_config_t pi_d;
    bm_algo_pi_config_t pi_q;
    bm_algo_flux_observer_config_t observer;
} bm_motor_foc_sensorless_config_t;

typedef struct {
    bm_motor_sl_cmd_t cmd;
    bm_motor_sl_telemetry_t telemetry;
    bm_motor_sl_phase_t phase;
    float phase_timer_s;
    float open_loop_theta;
    float open_loop_omega;
    float lock_loss_timer_s;
    bm_algo_pi_state_t pi_d;
    bm_algo_pi_state_t pi_q;
    bm_algo_flux_observer_state_t observer;
    float last_vd_pu;
    float last_vq_pu;
    uint32_t loop_count;
    int fault_latched;
    uint32_t fault_count;
} bm_motor_foc_sensorless_state_t;

typedef struct {
    bm_motor_foc_sensorless_config_t    config;
    bm_motor_foc_sensorless_resources_t resources;
    bm_motor_foc_sensorless_state_t     state;
} bm_motor_foc_sensorless_axis_t;

int bm_motor_foc_sensorless_validate_config(
    const bm_motor_foc_sensorless_config_t *config);

void bm_motor_foc_sensorless_reset(bm_motor_foc_sensorless_axis_t *axis);

void bm_motor_foc_sensorless_apply_command(bm_motor_foc_sensorless_axis_t *axis,
                                           const bm_motor_sl_cmd_t *cmd);

void bm_motor_foc_sensorless_current_step(bm_motor_foc_sensorless_axis_t *axis);

void bm_motor_foc_sensorless_exec_current(const bm_exec_t *instance);

int bm_motor_foc_sensorless_exec_init(const bm_exec_t *instance);

int bm_motor_foc_sensorless_exec_start(const bm_exec_t *instance);

void bm_motor_foc_sensorless_exec_safe_stop(const bm_exec_t *instance);

extern const bm_exec_ops_t bm_motor_foc_sensorless_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTOR_FOC_SENSORLESS_H */

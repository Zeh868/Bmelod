/**
 * @file motor_foc_sensored.h
 * @brief 有感 FOC 伺服轴领域组件（电流环 + 速度环 + 编码器）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 *
 * 单实例包含双 HRT 槽语义：快环电流、慢环速度。命令/遥测由应用经 snapshot 注入。
 * HAL 句柄经 resources 注入，不包含板级初始化。
 */
#ifndef BM_MOTOR_FOC_SENSORED_H
#define BM_MOTOR_FOC_SENSORED_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_motion.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"
#include "hal/bm_hal_adc.h"
#include "hal/bm_hal_encoder.h"
#include "hal/bm_hal_pwm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_MOTOR_FOC_CMD_ENABLED  (1u << 0u)
#define BM_MOTOR_FOC_CMD_FAULT    (1u << 1u)

#define BM_MOTOR_FOC_TEL_VALID    (1u << 0u)
#define BM_MOTOR_FOC_TEL_SAT      (1u << 1u)
#define BM_MOTOR_FOC_TEL_FAULT    (1u << 2u)

/** SRT → HRT 命令（由应用写入 state.cmd） */
typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    speed_setpoint_rad_s;
    float    id_ref_a;
} bm_motor_foc_cmd_t;

/** HRT → SRT 遥测（由组件写入 state.telemetry） */
typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    id_meas_a;
    float    iq_meas_a;
    float    speed_rad_s;
    float    theta_elec_rad;
    float    iq_ref_a;
} bm_motor_foc_telemetry_t;

/**
 * @brief native_sim 可选：注入 plant 电流/角度，绕开 ADC 极性偏差（实机置 NULL）
 */
typedef struct {
    float *id_a;
    float *iq_a;
    float *theta_elec_rad;
} bm_motor_foc_sim_feedback_t;

/**
 * @brief 电压施加后回调（Demo plant：RL + 机械模型 + 编码器计数）
 */
typedef void (*bm_motor_foc_on_voltage_fn)(void *user,
                                           float vd_pu,
                                           float vq_pu,
                                           float theta_elec_rad);

typedef int (*bm_motor_foc_read_command_fn)(void *user,
                                            bm_motor_foc_cmd_t *command);

typedef void (*bm_motor_foc_publish_telemetry_fn)(
    void *user,
    const bm_motor_foc_telemetry_t *telemetry);

typedef struct {
    const bm_hal_adc_t *adc;
    const bm_hal_pwm_t *pwm;
    const bm_hal_encoder_t *encoder;
    uint32_t adc_rank_ia;
    uint32_t adc_rank_ib;
    uint16_t pwm_max;
    float    current_adc_scale;
    bm_motor_foc_sim_feedback_t sim_fb;
    bm_motor_foc_on_voltage_fn on_voltage;
    void    *on_voltage_user;
    bm_motor_foc_read_command_fn read_command;
    void    *read_command_user;
    bm_motor_foc_publish_telemetry_fn publish_telemetry;
    void    *publish_telemetry_user;
} bm_motor_foc_sensored_resources_t;

typedef struct {
    float    pole_pairs;
    float    encoder_direction;
    float    electrical_offset_rad;
    float    vbus_v;
    float    phase_r_ohm;
    float    v_max_pu;
    float    current_dt_s;
    float    speed_dt_s;
    float    iq_max_a;
    bm_algo_pi_config_t pi_d;
    bm_algo_pi_config_t pi_q;
    bm_algo_pi_config_t pi_speed;
    bm_algo_ramp_config_t speed_ramp;
    bm_algo_encoder_config_t encoder;
} bm_motor_foc_sensored_config_t;

/** 快环状态（仅电流槽写） */
typedef struct {
    bm_algo_pi_state_t pi_d;
    bm_algo_pi_state_t pi_q;
    uint32_t           loop_count;
} bm_motor_foc_current_state_t;

/** 慢环状态（仅速度槽写） */
typedef struct {
    bm_algo_pi_state_t      pi_speed;
    bm_algo_ramp_state_t    speed_ramp;
    bm_algo_encoder_state_t encoder;
    float                   iq_ref_a;
    uint32_t                last_cmd_sequence;
} bm_motor_foc_speed_state_t;

typedef struct {
    bm_motor_foc_cmd_t        cmd;
    bm_motor_foc_telemetry_t  telemetry;
    bm_motor_foc_current_state_t current;
    bm_motor_foc_speed_state_t   speed;
    int                       fault_latched;
    uint32_t                  fault_count;
} bm_motor_foc_sensored_state_t;

typedef struct {
    bm_motor_foc_sensored_config_t     config;
    bm_motor_foc_sensored_resources_t  resources;
    bm_motor_foc_sensored_state_t        state;
} bm_motor_foc_sensored_axis_t;

int bm_motor_foc_sensored_validate_config(
    const bm_motor_foc_sensored_config_t *config);

void bm_motor_foc_sensored_reset(bm_motor_foc_sensored_axis_t *axis);

void bm_motor_foc_sensored_apply_command(bm_motor_foc_sensored_axis_t *axis,
                                        const bm_motor_foc_cmd_t *cmd);

void bm_motor_foc_sensored_current_step(bm_motor_foc_sensored_axis_t *axis);

void bm_motor_foc_sensored_speed_step(bm_motor_foc_sensored_axis_t *axis);

/** bm_exec 槽回调：instance->state 须指向 bm_motor_foc_sensored_axis_t */
void bm_motor_foc_sensored_exec_current(const bm_exec_t *instance);

void bm_motor_foc_sensored_exec_speed(const bm_exec_t *instance);

int bm_motor_foc_sensored_exec_init(const bm_exec_t *instance);

int bm_motor_foc_sensored_exec_start(const bm_exec_t *instance);

void bm_motor_foc_sensored_exec_safe_stop(const bm_exec_t *instance);

extern const bm_exec_ops_t bm_motor_foc_sensored_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTOR_FOC_SENSORED_H */

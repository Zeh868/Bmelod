/**
 * @file power_control.h
 * @brief 数字电源 Buck 双环领域组件（电压环 + 电流环 + 软启动）
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
 */
#ifndef BM_POWER_CONTROL_H
#define BM_POWER_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_POWER_CTRL_CMD_ENABLED  (1u << 0u)
#define BM_POWER_CTRL_CMD_FAULT    (1u << 1u)

#define BM_POWER_CTRL_TEL_VALID    (1u << 0u)
#define BM_POWER_CTRL_TEL_SAT      (1u << 1u)
#define BM_POWER_CTRL_TEL_FAULT    (1u << 2u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    v_set_v;
    float    v_out_v;
    float    i_out_a;
    float    duty;
} bm_power_ctrl_telemetry_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    v_set_v;
} bm_power_ctrl_cmd_t;

typedef int (*bm_power_ctrl_read_feedback_fn)(void *user,
                                            float *v_out_v,
                                            float *i_out_a);

typedef int (*bm_power_ctrl_write_duty_fn)(void *user, float duty);

typedef int (*bm_power_ctrl_read_command_fn)(void *user,
                                             bm_power_ctrl_cmd_t *command);

typedef void (*bm_power_ctrl_publish_telemetry_fn)(
    void *user,
    const bm_power_ctrl_telemetry_t *telemetry);

typedef struct {
    bm_power_ctrl_read_feedback_fn read_feedback;
    void                          *read_feedback_user;
    bm_power_ctrl_write_duty_fn    write_duty;
    void                          *write_duty_user;
    bm_power_ctrl_read_command_fn    read_command;
    void                          *read_command_user;
    bm_power_ctrl_publish_telemetry_fn publish_telemetry;
    void                          *publish_telemetry_user;
} bm_power_control_resources_t;

typedef struct {
    bm_algo_pi_config_t    pi_voltage;
    bm_algo_pi_config_t    pi_current;
    bm_algo_ramp_config_t  v_ramp;
    float                  i_limit_a;
    float                  duty_min;
    float                  duty_max;
    float                  voltage_dt_s;
    float                  current_dt_s;
} bm_power_control_config_t;

typedef struct {
    bm_algo_pi_state_t   pi_voltage;
    bm_algo_pi_state_t   pi_current;
    bm_algo_ramp_state_t v_ramp;
    float                i_ref_a;
    float                duty;
    float                v_target_v;
    bm_power_ctrl_cmd_t  cmd;
    bm_power_ctrl_telemetry_t telemetry;
    int                  fault_latched;
    uint32_t             voltage_loops;
    uint32_t             current_loops;
} bm_power_control_state_t;

typedef struct {
    bm_power_control_config_t    config;
    bm_power_control_resources_t resources;
    bm_power_control_state_t     state;
} bm_power_control_axis_t;

int bm_power_control_validate_config(const bm_power_control_config_t *config);

void bm_power_control_reset(bm_power_control_axis_t *axis);

void bm_power_control_apply_command(bm_power_control_axis_t *axis,
                                   const bm_power_ctrl_cmd_t *cmd);

/** 慢环：电压 PI → 电流参考 */
void bm_power_control_voltage_step(bm_power_control_axis_t *axis);

/** 快环：电流 PI → 占空比 */
void bm_power_control_current_step(bm_power_control_axis_t *axis);

void bm_power_control_exec_voltage(const bm_exec_t *instance);

void bm_power_control_exec_current(const bm_exec_t *instance);

int bm_power_control_exec_init(const bm_exec_t *instance);

int bm_power_control_exec_start(const bm_exec_t *instance);

void bm_power_control_exec_safe_stop(const bm_exec_t *instance);

extern const bm_exec_ops_t bm_power_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_POWER_CONTROL_H */

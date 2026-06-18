/**
 * @file power_converter.h
 * @brief Buck 峰值电流模式领域组件（电流参考斜坡 + 电流 PI）
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
#ifndef BM_POWER_CONVERTER_H
#define BM_POWER_CONVERTER_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_PWR_CONV_CMD_ENABLED  (1u << 0u)
#define BM_PWR_CONV_CMD_FAULT    (1u << 1u)

#define BM_PWR_CONV_TEL_VALID    (1u << 0u)
#define BM_PWR_CONV_TEL_SAT      (1u << 1u)
#define BM_PWR_CONV_TEL_FAULT    (1u << 2u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    i_set_a;
    float    i_out_a;
    float    i_ref_a;
    float    duty;
} bm_pwr_conv_telemetry_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    i_set_a;
} bm_pwr_conv_cmd_t;

typedef int (*bm_pwr_conv_read_current_fn)(void *user, float *i_out_a);

typedef int (*bm_pwr_conv_write_duty_fn)(void *user, float duty);

typedef int (*bm_pwr_conv_read_command_fn)(void *user,
                                           bm_pwr_conv_cmd_t *command);

typedef void (*bm_pwr_conv_publish_telemetry_fn)(
    void *user,
    const bm_pwr_conv_telemetry_t *telemetry);

typedef struct {
    bm_pwr_conv_read_current_fn       read_current;
    void                             *read_current_user;
    bm_pwr_conv_write_duty_fn         write_duty;
    void                             *write_duty_user;
    bm_pwr_conv_read_command_fn       read_command;
    void                             *read_command_user;
    bm_pwr_conv_publish_telemetry_fn  publish_telemetry;
    void                             *publish_telemetry_user;
} bm_power_converter_resources_t;

typedef struct {
    bm_algo_pi_config_t    pi_current;
    bm_algo_ramp_config_t  i_ramp;
    float                  duty_min;
    float                  duty_max;
    float                  current_dt_s;
} bm_power_converter_config_t;

typedef struct {
    bm_algo_pi_state_t   pi_current;
    bm_algo_ramp_state_t i_ramp;
    float                i_ref_a;
    float                duty;
    bm_pwr_conv_cmd_t    cmd;
    bm_pwr_conv_telemetry_t telemetry;
    int                  fault_latched;
    uint32_t             current_loops;
} bm_power_converter_state_t;

typedef struct {
    bm_power_converter_config_t    config;
    bm_power_converter_resources_t resources;
    bm_power_converter_state_t     state;
} bm_power_converter_axis_t;

int bm_power_converter_validate_config(const bm_power_converter_config_t *config);

void bm_power_converter_reset(bm_power_converter_axis_t *axis);
void bm_power_converter_clear_fault(bm_power_converter_axis_t *axis);

void bm_power_converter_apply_command(bm_power_converter_axis_t *axis,
                                      const bm_pwr_conv_cmd_t *cmd);

/** 快环：电流参考斜坡 + 电流 PI → 占空比 */
void bm_power_converter_current_step(bm_power_converter_axis_t *axis);

void bm_power_converter_exec_current(const bm_exec_t *instance);

int bm_power_converter_exec_init(const bm_exec_t *instance);

int bm_power_converter_exec_start(const bm_exec_t *instance);

void bm_power_converter_exec_safe_stop(const bm_exec_t *instance);

extern const bm_exec_ops_t bm_power_converter_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_POWER_CONVERTER_H */

/**
 * @file process_control.h
 * @brief 过程控制：Smith 预估器 + PID 串级骨架
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
#ifndef BM_PROCESS_CONTROL_H
#define BM_PROCESS_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_PROCESS_CTRL_TEL_VALID (1u << 0u)
#define BM_PROCESS_CTRL_TEL_STALE (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    setpoint;
    float    measurement;
    float    outer_out;
    float    inner_out;
} bm_process_control_telemetry_t;

typedef int (*bm_process_control_read_fn)(void *user,
                                          float *setpoint,
                                          float *measurement);

typedef int (*bm_process_control_write_fn)(void *user, float output);

typedef void (*bm_process_control_publish_fn)(
    void *user,
    const bm_process_control_telemetry_t *telemetry);

typedef struct {
    bm_process_control_read_fn    read_io;
    void                         *read_io_user;
    bm_process_control_write_fn   write_output;
    void                         *write_output_user;
    bm_process_control_publish_fn publish_telemetry;
    void                         *publish_telemetry_user;
} bm_process_control_resources_t;

typedef struct {
    bm_algo_pid_config_t              outer_pid;
    bm_algo_pid_config_t              inner_pid;
    bm_algo_smith_predictor_config_t  smith;
    float                            *smith_delay_line;
    uint32_t                          smith_line_len;
    float                             dt_s;
} bm_process_control_config_t;

typedef struct {
    bm_algo_pid_state_t              outer_pid;
    bm_algo_pid_state_t              inner_pid;
    bm_algo_smith_predictor_state_t  smith;
    float                            outer_out;
    float                            inner_out;
    uint32_t                         step_count;
    bm_process_control_telemetry_t   telemetry;
} bm_process_control_state_t;

typedef struct {
    bm_process_control_config_t    config;
    bm_process_control_resources_t resources;
    bm_process_control_state_t     state;
} bm_process_control_axis_t;

int  bm_process_control_validate_config(const bm_process_control_config_t *config);
int  bm_process_control_init(bm_process_control_axis_t *axis);
void bm_process_control_reset(bm_process_control_axis_t *axis);
void bm_process_control_step(bm_process_control_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_PROCESS_CONTROL_H */

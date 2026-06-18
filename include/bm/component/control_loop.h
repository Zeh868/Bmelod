/**
 * @file control_loop.h
 * @brief 串级 PI 控制环骨架（外环设定 → 内环跟踪）
 *
 * 外环输出作为内环设定；饱和与抗饱和由 bm_algo_pi 承担。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始 K1 骨架
 */
#ifndef BM_CONTROL_LOOP_H
#define BM_CONTROL_LOOP_H

#include "bm/algorithm/bm_algo_control.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bm_algo_pi_config_t outer_pi;
    bm_algo_pi_config_t inner_pi;
    float               dt_s;
} bm_control_loop_config_t;

typedef int (*bm_control_loop_read_plant_fn)(void *user,
                                             float *outer_measurement,
                                             float *inner_measurement,
                                             float *setpoint);

typedef int (*bm_control_loop_write_output_fn)(void *user, float output);

typedef struct {
    bm_control_loop_read_plant_fn read_plant;
    void                           *read_plant_user;
    bm_control_loop_write_output_fn write_output;
    void                           *write_output_user;
} bm_control_loop_resources_t;

typedef struct {
    bm_algo_pi_state_t outer_pi;
    bm_algo_pi_state_t inner_pi;
    float              outer_out;
    float              inner_out;
    uint32_t           step_count;
} bm_control_loop_state_t;

typedef struct {
    bm_control_loop_config_t    config;
    bm_control_loop_resources_t resources;
    bm_control_loop_state_t     state;
} bm_control_loop_axis_t;

int  bm_control_loop_validate_config(const bm_control_loop_config_t *config);
void bm_control_loop_reset(bm_control_loop_axis_t *axis);
void bm_control_loop_step(bm_control_loop_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_CONTROL_LOOP_H */

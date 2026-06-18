/**
 * @file grid_control.h
 * @brief 并网控制：SOGI-PLL + PR 电流环骨架
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
#ifndef BM_GRID_CONTROL_H
#define BM_GRID_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_power.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_GRID_CTRL_TEL_VALID (1u << 0u)
#define BM_GRID_CTRL_TEL_STALE (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    theta_rad;
    float    omega_rad_s;
    float    i_ref_a;
    float    i_meas_a;
    float    v_cmd;
} bm_grid_control_telemetry_t;

typedef int (*bm_grid_control_read_fn)(void *user,
                                       float *v_grid,
                                       float *i_meas,
                                       float *i_ref);

typedef int (*bm_grid_control_write_fn)(void *user, float v_cmd);

typedef void (*bm_grid_control_publish_fn)(
    void *user,
    const bm_grid_control_telemetry_t *telemetry);

typedef struct {
    bm_grid_control_read_fn    read_io;
    void                      *read_io_user;
    bm_grid_control_write_fn   write_output;
    void                      *write_output_user;
    bm_grid_control_publish_fn publish_telemetry;
    void                      *publish_telemetry_user;
} bm_grid_control_resources_t;

typedef struct {
    bm_algo_sogi_pll_config_t pll;
    bm_algo_pr_config_t       pr_current;
    float                     dt_s;
} bm_grid_control_config_t;

typedef struct {
    bm_algo_sogi_pll_state_t pll;
    bm_algo_pr_state_t       pr_current;
    float                    pr_b0, pr_b1, pr_b2, pr_a1, pr_a2;
    float                    theta_rad;
    float                    omega_rad_s;
    float                    v_cmd;
    uint32_t                 step_count;
    bm_grid_control_telemetry_t telemetry;
} bm_grid_control_state_t;

typedef struct {
    bm_grid_control_config_t    config;
    bm_grid_control_resources_t resources;
    bm_grid_control_state_t     state;
} bm_grid_control_axis_t;

int  bm_grid_control_validate_config(const bm_grid_control_config_t *config);
int  bm_grid_control_init(bm_grid_control_axis_t *axis);
void bm_grid_control_reset(bm_grid_control_axis_t *axis);
void bm_grid_control_step(bm_grid_control_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_GRID_CONTROL_H */

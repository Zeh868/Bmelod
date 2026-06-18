/**
 * @file solar_control.h
 * @brief 光伏 MPPT 编排与限功率骨架
 *
 * 封装 P&O/增量电导 MPPT 与功率限额降额，输出工作点参考。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#ifndef BM_SOLAR_CONTROL_H
#define BM_SOLAR_CONTROL_H

#include "bm/algorithm/bm_algo_power.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_SOLAR_CTRL_TEL_VALID     (1u << 0u)
#define BM_SOLAR_CTRL_TEL_LIMITED   (1u << 1u)
#define BM_SOLAR_CTRL_TEL_STALE     (1u << 2u)

typedef enum {
    BM_SOLAR_MPPT_PO = 0,
    BM_SOLAR_MPPT_IC
} bm_solar_mppt_mode_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    voltage_v;
    float    current_a;
    float    power_w;
    float    v_ref_v;
} bm_solar_control_telemetry_t;

typedef int (*bm_solar_read_iv_fn)(void *user,
                                   float *voltage_v,
                                   float *current_a);

typedef int (*bm_solar_write_vref_fn)(void *user, float v_ref_v);

typedef void (*bm_solar_publish_fn)(void *user,
                                    const bm_solar_control_telemetry_t *telemetry);

typedef struct {
    bm_solar_read_iv_fn    read_iv;
    void                  *read_iv_user;
    bm_solar_write_vref_fn write_vref;
    void                  *write_vref_user;
    bm_solar_publish_fn    publish_telemetry;
    void                  *publish_telemetry_user;
} bm_solar_control_resources_t;

typedef struct {
    bm_solar_mppt_mode_t       mppt_mode;
    bm_algo_mppt_po_config_t   mppt_po;
    bm_algo_mppt_ic_config_t   mppt_ic;
    float                      power_limit_w;
    float                      v_init_v;
} bm_solar_control_config_t;

typedef struct {
    bm_algo_mppt_po_state_t po;
    bm_algo_mppt_ic_state_t ic;
    float v_ref_v;
    float last_power_w;
    uint32_t step_count;
    bm_solar_control_telemetry_t telemetry;
} bm_solar_control_state_t;

typedef struct {
    bm_solar_control_config_t    config;
    bm_solar_control_resources_t resources;
    bm_solar_control_state_t     state;
} bm_solar_control_axis_t;

int  bm_solar_control_validate_config(const bm_solar_control_config_t *config);
int  bm_solar_control_init(bm_solar_control_axis_t *axis);
void bm_solar_control_reset(bm_solar_control_axis_t *axis);
void bm_solar_control_step(bm_solar_control_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_SOLAR_CONTROL_H */

/**
 * @file bms_supervision.h
 * @brief BMS Pack 监督：限值检查与 fault_derating 集成
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
#ifndef BM_BMS_SUPERVISION_H
#define BM_BMS_SUPERVISION_H

#include "bm/component/fault_derating.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_BMS_SUP_TEL_VALID    (1u << 0u)
#define BM_BMS_SUP_TEL_DERATED  (1u << 1u)
#define BM_BMS_SUP_TEL_STALE    (1u << 2u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    pack_voltage_v;
    float    pack_current_a;
    float    temp_c;
    float    derate_factor;
    uint32_t limit_flags;
} bm_bms_supervision_telemetry_t;

typedef int (*bm_bms_supervision_read_sample_fn)(void *user,
                                                 float *voltage_v,
                                                 float *current_a,
                                                 float *temp_c);

typedef void (*bm_bms_supervision_publish_fn)(
    void *user,
    const bm_bms_supervision_telemetry_t *telemetry);

typedef struct {
    bm_bms_supervision_read_sample_fn read_sample;
    void                             *read_sample_user;
    bm_bms_supervision_publish_fn     publish_telemetry;
    void                             *publish_telemetry_user;
} bm_bms_supervision_resources_t;

#define BM_BMS_SUP_LIMIT_VOLTAGE_HIGH (1u << 0u)
#define BM_BMS_SUP_LIMIT_VOLTAGE_LOW  (1u << 1u)
#define BM_BMS_SUP_LIMIT_CURRENT      (1u << 2u)
#define BM_BMS_SUP_LIMIT_TEMP         (1u << 3u)

typedef struct {
    float v_max_v;
    float v_min_v;
    float i_max_a;
    float temp_max_c;
    float dt_s;
    bm_algo_ramp_config_t derate_ramp;
    float                 recovery_time_s;
    float                 derate_target;
} bm_bms_supervision_config_t;

typedef struct {
    bm_fault_derating_axis_t derating;
    uint32_t                 limit_flags;
    float                    pack_voltage_v;
    float                    pack_current_a;
    float                    temp_c;
    uint32_t                 step_count;
    bm_bms_supervision_telemetry_t telemetry;
} bm_bms_supervision_state_t;

typedef struct {
    bm_bms_supervision_config_t    config;
    bm_bms_supervision_resources_t resources;
    bm_bms_supervision_state_t     state;
} bm_bms_supervision_axis_t;

int  bm_bms_supervision_validate_config(const bm_bms_supervision_config_t *config);
int  bm_bms_supervision_init(bm_bms_supervision_axis_t *axis);
void bm_bms_supervision_reset(bm_bms_supervision_axis_t *axis);
void bm_bms_supervision_step(bm_bms_supervision_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_BMS_SUPERVISION_H */

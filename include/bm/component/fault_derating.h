/**
 * @file fault_derating.h
 * @brief 故障锁存、线性降额曲线与恢复定时器领域组件
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
#ifndef BM_FAULT_DERATING_H
#define BM_FAULT_DERATING_H

#include "bm/algorithm/bm_algo_profile.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_FAULT_DERATING_TEL_VALID   (1u << 0u)
#define BM_FAULT_DERATING_TEL_LATCHED (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    derate_factor;
    float    recovery_elapsed_s;
} bm_fault_derating_telemetry_t;

typedef void (*bm_fault_derating_publish_fn)(
    void *user,
    const bm_fault_derating_telemetry_t *telemetry);

typedef struct {
    bm_fault_derating_publish_fn publish_telemetry;
    void                        *publish_telemetry_user;
} bm_fault_derating_resources_t;

typedef struct {
    bm_algo_ramp_config_t derate_ramp;
    float                 recovery_time_s;
    float                 dt_s;
    float                 derate_target;
} bm_fault_derating_config_t;

typedef struct {
    bm_algo_ramp_state_t derate_ramp;
    int                  fault_latched;
    float                derate_factor;
    float                recovery_elapsed_s;
    uint32_t             step_count;
    bm_fault_derating_telemetry_t telemetry;
} bm_fault_derating_state_t;

typedef struct {
    bm_fault_derating_config_t    config;
    bm_fault_derating_resources_t resources;
    bm_fault_derating_state_t     state;
} bm_fault_derating_axis_t;

int  bm_fault_derating_validate_config(const bm_fault_derating_config_t *config);
int  bm_fault_derating_init(bm_fault_derating_axis_t *axis);
void bm_fault_derating_reset(bm_fault_derating_axis_t *axis);
void bm_fault_derating_latch(bm_fault_derating_axis_t *axis);
void bm_fault_derating_clear_request(bm_fault_derating_axis_t *axis);
void bm_fault_derating_step(bm_fault_derating_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_FAULT_DERATING_H */

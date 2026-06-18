/**
 * @file sensor_quality.h
 * @brief 传感器信号质量监控（范围、变化率、冻结值）
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
#ifndef BM_SENSOR_QUALITY_H
#define BM_SENSOR_QUALITY_H

#include "bm/algorithm/bm_algo_signal_quality.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_SENSOR_QUALITY_TEL_VALID (1u << 0u)
#define BM_SENSOR_QUALITY_TEL_STALE (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    value;
    uint32_t fault_flags;
} bm_sensor_quality_telemetry_t;

typedef int (*bm_sensor_quality_read_sample_fn)(void *user, float *sample);

typedef void (*bm_sensor_quality_publish_fn)(
    void *user,
    const bm_sensor_quality_telemetry_t *telemetry);

typedef struct {
    bm_sensor_quality_read_sample_fn read_sample;
    void                            *read_sample_user;
    bm_sensor_quality_publish_fn     publish_telemetry;
    void                            *publish_telemetry_user;
} bm_sensor_quality_resources_t;

typedef struct {
    bm_algo_range_monitor_config_t monitor;
    float                          frozen_epsilon;
    uint32_t                       frozen_count_required;
    float                          dt_s;
} bm_sensor_quality_config_t;

typedef struct {
    bm_algo_range_monitor_state_t monitor;
    float                         frozen_prev;
    uint32_t                      frozen_count;
    uint32_t                      fault_flags;
    float                         last_value;
    uint32_t                      step_count;
    bm_sensor_quality_telemetry_t telemetry;
} bm_sensor_quality_state_t;

typedef struct {
    bm_sensor_quality_config_t    config;
    bm_sensor_quality_resources_t resources;
    bm_sensor_quality_state_t     state;
} bm_sensor_quality_axis_t;

int  bm_sensor_quality_validate_config(const bm_sensor_quality_config_t *config);
int  bm_sensor_quality_init(bm_sensor_quality_axis_t *axis, float initial);
void bm_sensor_quality_reset(bm_sensor_quality_axis_t *axis, float initial);
void bm_sensor_quality_step(bm_sensor_quality_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_SENSOR_QUALITY_H */

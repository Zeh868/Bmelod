/**
 * @file motion_coordination.h
 * @brief 多轴斜坡/轨迹协调骨架
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
#ifndef BM_MOTION_COORDINATION_H
#define BM_MOTION_COORDINATION_H

#include "bm/algorithm/bm_algo_profile.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_MOTION_COORD_MAX_AXES 4u
#define BM_MOTION_COORD_TEL_VALID (1u << 0u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    uint32_t axis_count;
    float    position[BM_MOTION_COORD_MAX_AXES];
} bm_motion_coordination_telemetry_t;

typedef void (*bm_motion_coordination_publish_fn)(
    void *user,
    const bm_motion_coordination_telemetry_t *telemetry);

typedef struct {
    bm_motion_coordination_publish_fn publish_telemetry;
    void                             *publish_telemetry_user;
} bm_motion_coordination_resources_t;

typedef struct {
    uint32_t              axis_count;
    bm_algo_ramp_config_t ramp[BM_MOTION_COORD_MAX_AXES];
    float                 dt_s;
} bm_motion_coordination_config_t;

typedef struct {
    bm_algo_ramp_state_t ramp[BM_MOTION_COORD_MAX_AXES];
    float                target[BM_MOTION_COORD_MAX_AXES];
    uint32_t             step_count;
    bm_motion_coordination_telemetry_t telemetry;
} bm_motion_coordination_state_t;

typedef struct {
    bm_motion_coordination_config_t    config;
    bm_motion_coordination_resources_t resources;
    bm_motion_coordination_state_t     state;
} bm_motion_coordination_axis_t;

int  bm_motion_coordination_validate_config(
    const bm_motion_coordination_config_t *config);
int  bm_motion_coordination_init(bm_motion_coordination_axis_t *axis);
void bm_motion_coordination_reset(bm_motion_coordination_axis_t *axis,
                                  const float *initial);
void bm_motion_coordination_set_targets(bm_motion_coordination_axis_t *axis,
                                        const float *targets);
void bm_motion_coordination_step(bm_motion_coordination_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTION_COORDINATION_H */

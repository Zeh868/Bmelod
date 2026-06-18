/**
 * @file motion_profile.h
 * @brief 单轴运动轨迹规划（梯形或 S 曲线）
 *
 * 接收目标位置命令，按 jerk/速度/加速度约束输出位置与速度。
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
#ifndef BM_MOTION_PROFILE_H
#define BM_MOTION_PROFILE_H

#include "bm/algorithm/bm_algo_profile.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BM_MOTION_PROFILE_TRAP = 0,
    BM_MOTION_PROFILE_SCURVE = 1
} bm_motion_profile_type_t;

typedef struct {
    bm_motion_profile_type_t type;
    float                    jerk;
    float                    vmax;
    float                    amax;
    float                    dt_s;
} bm_motion_profile_config_t;

typedef struct {
    bm_algo_trapezoid_state_t trapezoid;
    bm_algo_scurve_state_t    scurve;
    float                     target_pos;
    int                       active;
    uint32_t                  step_count;
} bm_motion_profile_state_t;

typedef struct {
    float position;
    float velocity;
    int   done;
} bm_motion_profile_output_t;

typedef struct {
    bm_motion_profile_config_t config;
    bm_motion_profile_state_t  state;
} bm_motion_profile_axis_t;

int  bm_motion_profile_validate_config(const bm_motion_profile_config_t *config);

void bm_motion_profile_reset(bm_motion_profile_axis_t *axis, float position);

void bm_motion_profile_goto(bm_motion_profile_axis_t *axis, float position);

void bm_motion_profile_step(bm_motion_profile_axis_t *axis,
                            bm_motion_profile_output_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTION_PROFILE_H */

/**
 * @file additive_motion.h
 * @brief 增材 Z 轴输入整形（ZV 两脉冲 E1）
 *
 * 对 Z 轴位置指令施加零振动整形，降低共振激励。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            ZV 两脉冲骨架
 * 2026-06-17       0.2            zeh            pressure advance 线性模型
 */
#ifndef BM_ADDITIVE_MOTION_H
#define BM_ADDITIVE_MOTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_ADDITIVE_ZV_BUFFER_MAX  64u

typedef struct {
    uint32_t sequence;
    float    raw_cmd_mm;
    float    shaped_cmd_mm;
    float    velocity_mm_s;
} bm_additive_motion_telemetry_t;

typedef int (*bm_additive_read_z_fn)(void *user, float *position_mm);

typedef int (*bm_additive_write_z_fn)(void *user, float position_mm);

typedef void (*bm_additive_publish_fn)(
    void *user,
    const bm_additive_motion_telemetry_t *telemetry);

typedef struct {
    bm_additive_read_z_fn     read_z;
    void                     *read_z_user;
    bm_additive_write_z_fn    write_z;
    void                     *write_z_user;
    bm_additive_publish_fn    publish_telemetry;
    void                     *publish_telemetry_user;
} bm_additive_motion_resources_t;

typedef struct {
    float natural_freq_hz;
    float damping_ratio;
    float dt_s;
    float max_velocity_mm_s;
} bm_additive_motion_config_t;

typedef struct {
    float buffer[BM_ADDITIVE_ZV_BUFFER_MAX];
    uint32_t buffer_len;
    uint32_t buffer_head;
    float a0;
    float a1;
    float delay_s;
    uint32_t delay_steps;
    float last_cmd_mm;
    float shaped_mm;
    uint32_t step_count;
    bm_additive_motion_telemetry_t telemetry;
} bm_additive_motion_state_t;

typedef struct {
    bm_additive_motion_config_t    config;
    bm_additive_motion_resources_t resources;
    bm_additive_motion_state_t     state;
} bm_additive_motion_axis_t;

int  bm_additive_motion_validate_config(const bm_additive_motion_config_t *config);
int  bm_additive_motion_init(bm_additive_motion_axis_t *axis);
void bm_additive_motion_reset(bm_additive_motion_axis_t *axis);
void bm_additive_motion_shape_cmd(bm_additive_motion_axis_t *axis,
                                  float cmd_mm);
void bm_additive_motion_step(bm_additive_motion_axis_t *axis);

/** E1 线性挤出：extrusion_mm = velocity_mm_s * factor */
float bm_additive_motion_pressure_advance(float velocity_mm_s, float factor);

#ifdef __cplusplus
}
#endif

#endif /* BM_ADDITIVE_MOTION_H */

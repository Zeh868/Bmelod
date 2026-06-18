/**
 * @file mobile_base_control.h
 * @brief 差速底盘运动学：线速度/角速度 → 左右轮速
 *
 * 可选坡道重力前馈骨架；E1 双轮差速模型。
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
#ifndef BM_MOBILE_BASE_CONTROL_H
#define BM_MOBILE_BASE_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sequence;
    float    linear_m_s;
    float    angular_rad_s;
    float    left_wheel_m_s;
    float    right_wheel_m_s;
    float    slope_feedforward_m_s;
} bm_mobile_base_telemetry_t;

typedef int (*bm_mobile_base_write_wheels_fn)(void *user,
                                              float left_m_s,
                                              float right_m_s);

typedef void (*bm_mobile_base_publish_fn)(
    void *user,
    const bm_mobile_base_telemetry_t *telemetry);

typedef struct {
    bm_mobile_base_write_wheels_fn write_wheels;
    void                          *write_wheels_user;
    bm_mobile_base_publish_fn      publish_telemetry;
    void                          *publish_telemetry_user;
} bm_mobile_base_control_resources_t;

typedef struct {
    float wheel_base_m;
    float wheel_radius_m;
    float max_wheel_m_s;
    float slope_angle_rad;
    float slope_feedforward_gain;
    int   enable_slope_feedforward;
} bm_mobile_base_control_config_t;

typedef struct {
    float linear_cmd_m_s;
    float angular_cmd_rad_s;
    float left_m_s;
    float right_m_s;
    uint32_t step_count;
    bm_mobile_base_telemetry_t telemetry;
} bm_mobile_base_control_state_t;

typedef struct {
    bm_mobile_base_control_config_t    config;
    bm_mobile_base_control_resources_t resources;
    bm_mobile_base_control_state_t     state;
} bm_mobile_base_control_axis_t;

int  bm_mobile_base_control_validate_config(
    const bm_mobile_base_control_config_t *config);
int  bm_mobile_base_control_init(bm_mobile_base_control_axis_t *axis);
void bm_mobile_base_control_reset(bm_mobile_base_control_axis_t *axis);
void bm_mobile_base_control_set_cmd(bm_mobile_base_control_axis_t *axis,
                                    float linear_m_s,
                                    float angular_rad_s);
void bm_mobile_base_control_step(bm_mobile_base_control_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_MOBILE_BASE_CONTROL_H */

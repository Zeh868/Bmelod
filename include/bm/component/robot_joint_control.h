/**
 * @file robot_joint_control.h
 * @brief 单关节力矩 PI 控制 + 位置/速度限幅 + 摩擦补偿
 *
 * 经 resources 回调读位置/速度、写力矩；E1 单轴骨架。
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
#ifndef BM_ROBOT_JOINT_CONTROL_H
#define BM_ROBOT_JOINT_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sequence;
    float    position_rad;
    float    velocity_rad_s;
    float    torque_nm;
    float    friction_ff_nm;
} bm_robot_joint_telemetry_t;

typedef int (*bm_robot_joint_read_fn)(void *user,
                                      float *position_rad,
                                      float *velocity_rad_s);

typedef int (*bm_robot_joint_write_torque_fn)(void *user, float torque_nm);

typedef void (*bm_robot_joint_publish_fn)(
    void *user,
    const bm_robot_joint_telemetry_t *telemetry);

typedef struct {
    bm_robot_joint_read_fn         read_joint;
    void                          *read_joint_user;
    bm_robot_joint_write_torque_fn write_torque;
    void                          *write_torque_user;
    bm_robot_joint_publish_fn      publish_telemetry;
    void                          *publish_telemetry_user;
} bm_robot_joint_control_resources_t;

typedef struct {
    bm_algo_pi_config_t pi;
    float               position_setpoint_rad;
    float               position_min_rad;
    float               position_max_rad;
    float               velocity_max_rad_s;
    float               torque_max_nm;
    float               coulomb_friction;
    float               viscous_friction;
    float               friction_deadband;
    float               dt_s;
} bm_robot_joint_control_config_t;

typedef struct {
    bm_algo_pi_state_t              pi;
    float                           torque_cmd_nm;
    uint32_t                        step_count;
    bm_robot_joint_telemetry_t      telemetry;
} bm_robot_joint_control_state_t;

typedef struct {
    bm_robot_joint_control_config_t    config;
    bm_robot_joint_control_resources_t resources;
    bm_robot_joint_control_state_t     state;
} bm_robot_joint_control_axis_t;

int  bm_robot_joint_control_validate_config(
    const bm_robot_joint_control_config_t *config);
int  bm_robot_joint_control_init(bm_robot_joint_control_axis_t *axis);
void bm_robot_joint_control_reset(bm_robot_joint_control_axis_t *axis);
void bm_robot_joint_control_step(bm_robot_joint_control_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_ROBOT_JOINT_CONTROL_H */

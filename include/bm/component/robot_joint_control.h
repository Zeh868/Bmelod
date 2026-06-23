/**
 * @file robot_joint_control.h
 * @brief 单关节力矩 PI 控制 + 位置/速度限幅 + 摩擦补偿
 *
 * 经 resources 回调读位置/速度、写力矩；E1 单轴骨架。
 * 通过 exec_ops 接入 bm_exec 生命周期（init/start/safe_stop）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 exec_ops、Doxygen、SPDX
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ROBOT_JOINT_CONTROL_H
#define BM_ROBOT_JOINT_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 关节遥测帧
 *
 * 每个控制步输出快照，通过 publish_telemetry 回调上报。
 */
typedef struct {
    uint32_t sequence;       /**< 步计数，用于检测帧丢失 */
    float    position_rad;   /**< 当前关节位置，单位 rad */
    float    velocity_rad_s; /**< 限幅后关节速度，单位 rad/s */
    float    torque_nm;      /**< 输出力矩指令，单位 N·m */
    float    friction_ff_nm; /**< 摩擦前馈补偿量，单位 N·m */
} bm_robot_joint_telemetry_t;

/**
 * @brief 读关节状态回调函数类型
 *
 * @param user            用户上下文指针
 * @param position_rad    输出当前位置，单位 rad
 * @param velocity_rad_s  输出当前速度，单位 rad/s
 * @return 0 成功；非 0 失败（步函数将跳过本周期控制）
 */
typedef int (*bm_robot_joint_read_fn)(void *user,
                                      float *position_rad,
                                      float *velocity_rad_s);

/**
 * @brief 写力矩回调函数类型
 *
 * @param user      用户上下文指针
 * @param torque_nm 力矩指令，单位 N·m
 * @return 0 成功；非 0 失败
 */
typedef int (*bm_robot_joint_write_torque_fn)(void *user, float torque_nm);

/**
 * @brief 遥测发布回调函数类型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前步遥测数据指针
 */
typedef void (*bm_robot_joint_publish_fn)(
    void *user,
    const bm_robot_joint_telemetry_t *telemetry);

/**
 * @brief 关节控制资源句柄
 *
 * 运行时回调绑定，由调用方在初始化前填充。
 */
typedef struct {
    bm_robot_joint_read_fn         read_joint;             /**< 读位置/速度回调 */
    void                          *read_joint_user;        /**< read_joint 用户上下文 */
    bm_robot_joint_write_torque_fn write_torque;           /**< 写力矩回调 */
    void                          *write_torque_user;      /**< write_torque 用户上下文 */
    bm_robot_joint_publish_fn      publish_telemetry;      /**< 遥测发布回调 */
    void                          *publish_telemetry_user; /**< publish_telemetry 用户上下文 */
} bm_robot_joint_control_resources_t;

/**
 * @brief 关节控制配置参数
 */
typedef struct {
    bm_algo_pi_config_t pi;                 /**< PI 控制器参数 */
    float               position_setpoint_rad; /**< 目标位置，单位 rad */
    float               position_min_rad;   /**< 软限位下界，单位 rad */
    float               position_max_rad;   /**< 软限位上界，单位 rad（须 >= min）*/
    float               velocity_max_rad_s; /**< 速度限幅幅值，单位 rad/s，必须 > 0 */
    float               torque_max_nm;      /**< 力矩输出限幅，单位 N·m，必须 > 0 */
    float               coulomb_friction;   /**< 库仑摩擦补偿系数，单位 N·m */
    float               viscous_friction;   /**< 粘性摩擦补偿系数，单位 N·m·s/rad */
    float               friction_deadband;  /**< 摩擦死区半宽，单位 rad/s */
    float               dt_s;              /**< 控制步长，单位 s，必须 > 0 */
} bm_robot_joint_control_config_t;

/**
 * @brief 关节控制运行时状态
 */
typedef struct {
    bm_algo_pi_state_t         pi;           /**< PI 控制器状态 */
    float                      torque_cmd_nm;/**< 上一步输出力矩，单位 N·m */
    uint32_t                   step_count;   /**< 累计控制步数 */
    bm_robot_joint_telemetry_t telemetry;    /**< 最近一步遥测快照 */
} bm_robot_joint_control_state_t;

/**
 * @brief 关节控制轴聚合体
 *
 * 持有 config / resources / state，作为 bm_exec_t::state 传递给 exec_ops。
 */
typedef struct {
    bm_robot_joint_control_config_t    config;    /**< 配置参数 */
    bm_robot_joint_control_resources_t resources; /**< 运行时回调 */
    bm_robot_joint_control_state_t     state;     /**< 运行时状态 */
} bm_robot_joint_control_axis_t;

/**
 * @brief 校验配置参数合法性
 *
 * @param config 待校验配置指针
 * @return BM_OK（0）合法；BM_ERR_INVALID（-1）非法
 */
int  bm_robot_joint_control_validate_config(
    const bm_robot_joint_control_config_t *config);

/**
 * @brief 初始化关节轴（校验配置 + 复位状态）
 *
 * @param axis 关节轴实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或 axis 为 NULL
 */
int  bm_robot_joint_control_init(bm_robot_joint_control_axis_t *axis);

/**
 * @brief 复位关节轴状态（PI 清零、力矩清零、遥测清零）
 *
 * @param axis 关节轴实例指针；为 NULL 时直接返回
 */
void bm_robot_joint_control_reset(bm_robot_joint_control_axis_t *axis);

/**
 * @brief 执行一个控制步
 *
 * 读关节状态 → 速度限幅 → 位置 PI → 摩擦前馈 → 力矩限幅 →
 * 写力矩 → 发布遥测。
 *
 * @param axis 关节轴实例指针；NULL 或配置非法时直接返回
 */
void bm_robot_joint_control_step(bm_robot_joint_control_axis_t *axis);

/**
 * @brief exec_ops 兼容周期步函数，转发至 bm_robot_joint_control_step()
 *
 * @param instance bm_exec_t 实例指针；instance->state 须指向
 *                 bm_robot_joint_control_axis_t
 */
void bm_robot_joint_control_exec_step(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并复位状态
 *
 * @param instance bm_exec_t 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 失败
 */
int  bm_robot_joint_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调：空操作，预留扩展
 *
 * @param instance bm_exec_t 实例指针
 * @return 始终返回 BM_OK
 */
int  bm_robot_joint_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：力矩归零并写入硬件
 *
 * 将力矩指令置零后立即调用 write_torque，确保关节安全停机。
 *
 * @param instance bm_exec_t 实例指针
 */
void bm_robot_joint_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief 关节控制 exec_ops 表，可直接赋值给 bm_exec_t::ops */
extern const bm_exec_ops_t bm_robot_joint_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_ROBOT_JOINT_CONTROL_H */

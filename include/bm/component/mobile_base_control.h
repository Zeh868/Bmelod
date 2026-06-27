/**
 * @file mobile_base_control.h
 * @brief 差速底盘运动学：线速度/角速度 → 左右轮速
 *
 * 可选坡道重力前馈骨架；E1 双轮差速模型。
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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_MOBILE_BASE_CONTROL_H
#define BM_MOBILE_BASE_CONTROL_H

#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 底盘遥测帧
 *
 * 每个控制步的输出快照，通过 publish_telemetry 回调上报。
 */
typedef struct {
    uint32_t sequence;               /**< 步计数，用于检测帧丢失 */
    float    linear_m_s;             /**< 输入线速度指令，单位 m/s */
    float    angular_rad_s;          /**< 输入角速度指令，单位 rad/s */
    float    left_wheel_m_s;         /**< 左轮输出线速度，单位 m/s */
    float    right_wheel_m_s;        /**< 右轮输出线速度，单位 m/s */
    float    slope_feedforward_m_s;  /**< 坡道前馈补偿量，单位 m/s */
} bm_mobile_base_telemetry_t;

/**
 * @brief 写轮速回调函数类型
 *
 * @param user       用户上下文指针
 * @param left_m_s   左轮线速度，单位 m/s
 * @param right_m_s  右轮线速度，单位 m/s
 * @return 0 成功；非 0 失败
 */
typedef int (*bm_mobile_base_write_wheels_fn)(void *user,
                                              float left_m_s,
                                              float right_m_s);

/**
 * @brief 遥测发布回调函数类型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前步遥测数据指针
 */
typedef void (*bm_mobile_base_publish_fn)(
    void *user,
    const bm_mobile_base_telemetry_t *telemetry);

/**
 * @brief 底盘控制资源句柄
 *
 * 运行时回调绑定，由调用方在初始化前填充。
 */
typedef struct {
    bm_mobile_base_write_wheels_fn write_wheels;          /**< 写轮速驱动回调 */
    void                          *write_wheels_user;     /**< write_wheels 用户上下文 */
    bm_mobile_base_publish_fn      publish_telemetry;     /**< 遥测发布回调 */
    void                          *publish_telemetry_user;/**< publish_telemetry 用户上下文 */
} bm_mobile_base_control_resources_t;

/**
 * @brief 底盘控制配置参数
 */
typedef struct {
    float wheel_base_m;            /**< 轮距，单位 m，必须 > 0 */
    float wheel_radius_m;          /**< 轮半径，单位 m，必须 > 0 */
    float max_wheel_m_s;           /**< 单轮最大线速度限幅，单位 m/s，必须 > 0 */
    float slope_angle_rad;         /**< 坡道倾角，单位 rad（仅 enable_slope_feedforward 时有效）*/
    float slope_feedforward_gain;  /**< 坡道前馈增益系数（量纲 s²/m）*/
    int   enable_slope_feedforward;/**< 非 0 则启用坡道重力前馈 */
} bm_mobile_base_control_config_t;

/**
 * @brief 底盘控制运行时状态
 */
typedef struct {
    float    linear_cmd_m_s;    /**< 当前线速度指令缓存，单位 m/s */
    float    angular_cmd_rad_s; /**< 当前角速度指令缓存，单位 rad/s */
    float    left_m_s;          /**< 上一步左轮输出，单位 m/s */
    float    right_m_s;         /**< 上一步右轮输出，单位 m/s */
    uint32_t step_count;        /**< 累计控制步数 */
    bm_mobile_base_telemetry_t telemetry; /**< 最近一步遥测快照 */
} bm_mobile_base_control_state_t;

/**
 * @brief 底盘控制轴聚合体
 *
 * 持有 config / resources / state，作为 bm_exec_t::state 传递给 exec_ops。
 */
typedef struct {
    bm_mobile_base_control_config_t    config;    /**< 配置参数 */
    bm_mobile_base_control_resources_t resources; /**< 运行时回调 */
    bm_mobile_base_control_state_t     state;     /**< 运行时状态 */
} bm_mobile_base_control_axis_t;

/**
 * @brief 校验配置参数合法性
 *
 * @param config 待校验配置指针
 * @return BM_OK（0）合法；BM_ERR_INVALID（-1）非法
 */
int  bm_mobile_base_control_validate_config(
    const bm_mobile_base_control_config_t *config);

/**
 * @brief 初始化底盘轴（校验配置 + 复位状态）
 *
 * @param axis 底盘轴实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或 axis 为 NULL
 */
int  bm_mobile_base_control_init(bm_mobile_base_control_axis_t *axis);

/**
 * @brief 复位底盘轴状态（速度指令、输出归零，遥测清零）
 *
 * @param axis 底盘轴实例指针；为 NULL 时直接返回
 */
void bm_mobile_base_control_reset(bm_mobile_base_control_axis_t *axis);

/**
 * @brief 设置速度指令
 *
 * 线程安全性由调用方保证；step() 在下一周期读取。
 *
 * @param axis         底盘轴实例指针
 * @param linear_m_s   线速度，单位 m/s
 * @param angular_rad_s 角速度，单位 rad/s
 */
void bm_mobile_base_control_set_cmd(bm_mobile_base_control_axis_t *axis,
                                    float linear_m_s,
                                    float angular_rad_s);

/**
 * @brief 执行一个控制步
 *
 * 将 (v, ω) 换算为左右轮速，叠加可选坡道前馈后限幅，
 * 调用 write_wheels 写入硬件，最后发布遥测。
 *
 * @param axis 底盘轴实例指针；NULL 或配置非法时直接返回
 */
void bm_mobile_base_control_step(bm_mobile_base_control_axis_t *axis);

/**
 * @brief exec_ops 兼容周期步函数，转发至 bm_mobile_base_control_step()
 *
 * @param instance bm_exec_t 实例指针；instance->state 须指向
 *                 bm_mobile_base_control_axis_t
 */
void bm_mobile_base_control_exec_step(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并复位状态
 *
 * @param instance bm_exec_t 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 失败
 */
int  bm_mobile_base_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调：空操作，预留扩展
 *
 * @param instance bm_exec_t 实例指针
 * @return 始终返回 BM_OK
 */
int  bm_mobile_base_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：速度归零并写入硬件
 *
 * 将左右轮速指令置零后立即调用 write_wheels，确保机器人安全停止。
 *
 * @param instance bm_exec_t 实例指针
 */
void bm_mobile_base_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief 底盘控制 exec_ops 表，可直接赋值给 bm_exec_t::ops */
extern const bm_exec_ops_t bm_mobile_base_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOBILE_BASE_CONTROL_H */

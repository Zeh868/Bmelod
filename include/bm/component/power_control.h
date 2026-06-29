/**
 * @file power_control.h
 * @brief 数字电源 Buck 双环领域组件（电压环 + 电流环 + 软启动）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_POWER_CONTROL_H
#define BM_POWER_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 命令状态位：使能电源输出 */
#define BM_POWER_CTRL_CMD_ENABLED  (1u << 0u)
/** @brief 命令状态位：外部故障锁定 */
#define BM_POWER_CTRL_CMD_FAULT    (1u << 1u)

/** @brief 遥测状态位：数据有效 */
#define BM_POWER_CTRL_TEL_VALID    (1u << 0u)
/** @brief 遥测状态位：电流参考已饱和 */
#define BM_POWER_CTRL_TEL_SAT      (1u << 1u)
/** @brief 遥测状态位：当前帧处于故障态 */
#define BM_POWER_CTRL_TEL_FAULT    (1u << 2u)

/**
 * @brief 电源控制遥测快照
 */
typedef struct {
    uint32_t sequence;  /**< 步计数（单调递增） */
    uint32_t status;    /**< 状态位组合（BM_POWER_CTRL_TEL_* 位域） */
    float    v_set_v;   /**< 当前有效电压设定（经斜坡后，V） */
    float    v_out_v;   /**< 实测输出电压（V） */
    float    i_out_a;   /**< 实测输出电流（A） */
    float    duty;      /**< 当前 PWM 占空比（[duty_min, duty_max]） */
} bm_power_ctrl_telemetry_t;

/**
 * @brief 电源控制命令
 */
typedef struct {
    uint32_t sequence; /**< 命令序列号 */
    uint32_t status;   /**< 命令状态位（BM_POWER_CTRL_CMD_* 位域） */
    float    v_set_v;  /**< 目标输出电压（V） */
} bm_power_ctrl_cmd_t;

/**
 * @brief 反馈读取回调函数类型
 *
 * @param user    用户上下文指针
 * @param v_out_v 输出：实测输出电压（V）
 * @param i_out_a 输出：实测输出电流（A）
 * @return 0 成功；非零 采样失败（触发故障锁存）
 */
typedef int (*bm_power_ctrl_read_feedback_fn)(void *user,
                                            float *v_out_v,
                                            float *i_out_a);

/**
 * @brief 占空比写入回调函数类型
 *
 * @param user 用户上下文指针
 * @param duty 待写入占空比（归一化，[0, 1]）
 * @return 0 成功；非零 写入失败（触发故障锁存）
 */
typedef int (*bm_power_ctrl_write_duty_fn)(void *user, float duty);

/**
 * @brief 命令读取回调函数类型
 *
 * @param user    用户上下文指针
 * @param command 输出：最新命令
 * @return 0 成功；非零 无新命令
 */
typedef int (*bm_power_ctrl_read_command_fn)(void *user,
                                             bm_power_ctrl_cmd_t *command);

/**
 * @brief 遥测发布回调函数类型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前帧遥测快照指针（只读）
 */
typedef void (*bm_power_ctrl_publish_telemetry_fn)(
    void *user,
    const bm_power_ctrl_telemetry_t *telemetry);

/**
 * @brief 电源控制外部资源（回调绑定）
 */
typedef struct {
    bm_power_ctrl_read_feedback_fn read_feedback;      /**< 反馈读取回调，可为 NULL */
    void                          *read_feedback_user;  /**< read_feedback 用户上下文 */
    bm_power_ctrl_write_duty_fn    write_duty;          /**< 占空比写入回调，可为 NULL */
    void                          *write_duty_user;     /**< write_duty 用户上下文 */
    bm_power_ctrl_read_command_fn    read_command;      /**< 命令读取回调，可为 NULL */
    void                          *read_command_user;   /**< read_command 用户上下文 */
    bm_power_ctrl_publish_telemetry_fn publish_telemetry; /**< 遥测发布回调，可为 NULL */
    void                          *publish_telemetry_user; /**< publish_telemetry 用户上下文 */
} bm_power_control_resources_t;

/**
 * @brief 电源控制静态配置
 */
typedef struct {
    bm_algo_pi_config_t    pi_voltage;   /**< 电压环 PI 配置 */
    bm_algo_pi_config_t    pi_current;   /**< 电流环 PI 配置 */
    bm_algo_ramp_config_t  v_ramp;       /**< 电压设定软启动斜坡配置 */
    float                  i_limit_a;    /**< 电流环输出限幅（A） */
    float                  duty_min;     /**< 最小允许占空比 */
    float                  duty_max;     /**< 最大允许占空比，须 > duty_min */
    float                  voltage_dt_s; /**< 电压环步长（s），须 > 0 */
    float                  current_dt_s; /**< 电流环步长（s），须 > 0 */
} bm_power_control_config_t;

/**
 * @brief 电源控制运行时状态
 */
typedef struct {
    bm_algo_pi_state_t   pi_voltage;    /**< 电压 PI 积分器状态 */
    bm_algo_pi_state_t   pi_current;    /**< 电流 PI 积分器状态 */
    bm_algo_ramp_state_t v_ramp;        /**< 电压软启动斜坡状态 */
    float                i_ref_a;       /**< 当前电流参考（由电压环输出，A） */
    float                duty;          /**< 当前占空比 */
    float                v_target_v;    /**< 经斜坡后的电压目标（V） */
    bm_power_ctrl_cmd_t  cmd;           /**< 最新控制命令 */
    bm_power_ctrl_telemetry_t telemetry; /**< 最新遥测快照 */
    int                  fault_latched; /**< 非零：故障已锁存 */
    uint32_t             voltage_loops; /**< 电压环执行次数 */
    uint32_t             current_loops; /**< 电流环执行次数 */
} bm_power_control_state_t;

/**
 * @brief 电源控制完整实例（配置 + 资源 + 状态）
 */
typedef struct {
    bm_power_control_config_t    config;    /**< 静态配置，初始化前填写 */
    bm_power_control_resources_t resources; /**< 外部回调绑定 */
    bm_power_control_state_t     state;     /**< 运行时状态，由 API 维护 */
} bm_power_control_axis_t;

/**
 * @brief 校验电源控制配置合法性
 *
 * @param config 指向待校验的配置结构体，不可为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数越界或指针为空
 */
int bm_power_control_validate_config(const bm_power_control_config_t *config);

/**
 * @brief 复位电源控制实例状态
 *
 * 清零两级 PI 积分器、斜坡状态及所有输出；占空比置为 duty_min。
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_power_control_reset(bm_power_control_axis_t *axis);

/**
 * @brief 应用外部控制命令（使能/故障/v_set_v）
 *
 * 若命令携带 FAULT 位则立即锁存故障并将占空比置为 duty_min。
 *
 * @param axis 实例指针
 * @param cmd  待应用的命令，不可为 NULL
 */
void bm_power_control_apply_command(bm_power_control_axis_t *axis,
                                   const bm_power_ctrl_cmd_t *cmd);

/**
 * @brief 执行一步电压环（慢环）：电压 PI 输出 → 电流参考
 *
 * 先通过 sync_command() 同步最新命令，再读取反馈，
 * 经软启动斜坡后运行电压 PI，输出限幅写入 i_ref_a。
 * 故障或未使能时将 i_ref_a 清零。
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_power_control_voltage_step(bm_power_control_axis_t *axis);

/**
 * @brief 执行一步电流环（快环）：电流 PI 输出 → 占空比
 *
 * 以 i_ref_a 为设定值，读取反馈电流，运行电流 PI，
 * 限幅后调用 write_duty 回调写入 PWM。
 * write_duty 返回非零时触发故障锁存。
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_power_control_current_step(bm_power_control_axis_t *axis);

/**
 * @brief exec 封装：执行一步电压环（供调度框架慢时基槽调用）
 *
 * @param instance exec 实例指针，instance->state 须为 bm_power_control_axis_t*
 */
void bm_power_control_exec_voltage(const bm_exec_t *instance);

/**
 * @brief exec 封装：执行一步电流环（供调度框架快时基槽调用）
 *
 * @param instance exec 实例指针，instance->state 须为 bm_power_control_axis_t*
 */
void bm_power_control_exec_current(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：初始化（校验配置并复位状态）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_power_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：启动（当前无额外操作，保留扩展点）
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_power_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：安全停机（电流参考归零，占空比降至 duty_min）
 *
 * 通过 write_duty 回调向 PWM 写入 duty_min，确保输出安全关断。
 *
 * @param instance exec 实例指针
 */
void bm_power_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief power_control 标准 exec ops 表，可直接赋给 bm_exec_t::ops */
extern const bm_exec_ops_t bm_power_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_POWER_CONTROL_H */

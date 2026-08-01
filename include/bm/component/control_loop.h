/**
 * @file control_loop.h
 * @brief 串级 PI 控制环骨架（外环设定 → 内环跟踪）
 *
 * 外环输出作为内环设定；饱和与抗饱和由 bm_algo_pi 承担。
 * 提供 bm_exec_ops_t 标准封装，可直接接入调度框架。
 * 默认未使能：须经 apply_command 置 ENABLED 后 step 才跑环。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.5
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始 K1 骨架
 * 2026-06-23       0.2            zeh            补 bm_exec_ops_t 标准调度封装接口
 * 2026-07-27       0.3            zeh            新增 bm_control_loop_init 四段式入口
 * 2026-07-27       0.4            zeh            init/validate 复用 bm_component_common.h 公共宏
 * 2026-08-01       0.5            zeh            对齐 power_control：CMD_ENABLED/FAULT 状态机
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_CONTROL_LOOP_H
#define BM_CONTROL_LOOP_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 命令状态位：使能控制环输出 */
#define BM_CONTROL_LOOP_CMD_ENABLED  (1u << 0u)
/** @brief 命令状态位：外部故障锁定 */
#define BM_CONTROL_LOOP_CMD_FAULT    (1u << 1u)

/**
 * @brief 串级控制环命令
 */
typedef struct {
    uint32_t sequence; /**< 命令序列号 */
    uint32_t status;   /**< 命令状态位（BM_CONTROL_LOOP_CMD_* 位域） */
} bm_control_loop_cmd_t;

typedef struct {
    bm_algo_pi_config_t outer_pi;
    bm_algo_pi_config_t inner_pi;
    float               dt_s;
} bm_control_loop_config_t;

typedef int (*bm_control_loop_read_plant_fn)(void *user,
                                             float *outer_measurement,
                                             float *inner_measurement,
                                             float *setpoint);

typedef int (*bm_control_loop_write_output_fn)(void *user, float output);

/**
 * @brief 命令读取回调函数类型
 *
 * @param user    用户上下文指针
 * @param command 输出：最新命令
 * @return 0 成功；非零 无新命令
 */
typedef int (*bm_control_loop_read_command_fn)(void *user,
                                               bm_control_loop_cmd_t *command);

typedef struct {
    bm_control_loop_read_plant_fn    read_plant;
    void                            *read_plant_user;
    bm_control_loop_write_output_fn  write_output;
    void                            *write_output_user;
    bm_control_loop_read_command_fn  read_command;      /**< 可为 NULL */
    void                            *read_command_user;
} bm_control_loop_resources_t;

typedef struct {
    bm_algo_pi_state_t    outer_pi;
    bm_algo_pi_state_t    inner_pi;
    float                 outer_out;
    float                 inner_out;
    uint32_t              step_count;
    bm_control_loop_cmd_t cmd;           /**< 最新控制命令 */
    int                   fault_latched; /**< 非零：故障已锁存 */
} bm_control_loop_state_t;

typedef struct {
    bm_control_loop_config_t    config;
    bm_control_loop_resources_t resources;
    bm_control_loop_state_t     state;
} bm_control_loop_axis_t;

/**
 * @brief 校验串级控制环配置
 * @param config 控制环配置
 * @return BM_OK 配置合法；BM_ERR_INVALID 参数非法
 */
int  bm_control_loop_validate_config(const bm_control_loop_config_t *config);

/**
 * @brief 复位串级控制环运行状态
 *
 * 清零输出与积分器，清除故障锁存与命令（默认未使能）。
 *
 * @param axis 控制环实例；NULL 时静默返回
 */
void bm_control_loop_reset(bm_control_loop_axis_t *axis);

/**
 * @brief 初始化 control_loop 轴实例
 *
 * 校验配置合法性，并将所有状态复位为零初值（未使能）。
 *
 * @param axis 控制轴实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_control_loop_init(bm_control_loop_axis_t *axis);

/**
 * @brief 应用外部控制命令（使能/故障）
 *
 * 若命令携带 FAULT 位则立即锁存故障并清零输出。
 *
 * @param axis 实例指针
 * @param cmd  待应用的命令，不可为 NULL
 */
void bm_control_loop_apply_command(bm_control_loop_axis_t *axis,
                                   const bm_control_loop_cmd_t *cmd);

/**
 * @brief 执行一次串级控制环计算
 *
 * 先 sync_command；故障或未 ENABLED 时清输出、复位积分器并返回。
 * read_plant 失败时锁存故障。
 *
 * @param axis 控制环实例；NULL 时静默返回
 */
void bm_control_loop_step(bm_control_loop_axis_t *axis);

/**
 * @brief exec 封装：运行一步串级 PI（供调度框架调用）
 *
 * 通过 instance->state 取得 bm_control_loop_axis_t 指针后调用
 * bm_control_loop_step()，保持与直接调用完全一致的行为。
 *
 * @param instance exec 实例指针，instance->state 须为 bm_control_loop_axis_t*
 */
void bm_control_loop_exec_step(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：初始化（校验配置并复位状态）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_control_loop_exec_init(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：启动（当前无额外操作，保留扩展点）
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_control_loop_exec_start(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：安全停机（输出归零、复位两级积分器）
 *
 * 调用后 inner_out/outer_out 归零，两级 PI 积分器复位，
 * 并通过 write_output 回调向执行器写入零值。
 *
 * @param instance exec 实例指针
 */
void bm_control_loop_exec_safe_stop(const bm_exec_t *instance);

/** @brief control_loop 标准 exec ops 表，可直接赋给 bm_exec_t::ops */
extern const bm_exec_ops_t bm_control_loop_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_CONTROL_LOOP_H */

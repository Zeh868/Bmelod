/**
 * @file grid_control.h
 * @brief 并网控制：SOGI-PLL + PR 电流环骨架
 *
 * 封装 SOGI-PLL 锁相与 PR 谐振电流环，提供 bm_exec_ops_t 调度接口。
 * 默认未使能：须经 apply_command 置 ENABLED 后 step 才跑环。
 * 故障锁存（fault_latched / CMD_FAULT）仅能经 reset 清除；清除命令 FAULT
 * 位不会自动解锁。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.5
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 exec_ops 声明；validate_config 增加 PLL/PR 参数校验
 * 2026-08-01       0.3            zeh            对齐 power_control：CMD_ENABLED/FAULT 状态机
 * 2026-08-01       0.4            zeh            exec_safe_stop 复位 PLL/PR；reset NULL 契约对齐
 * 2026-08-01       0.5            zeh            文档化故障仅 reset 清除；read_io 未绑定按零值
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_GRID_CONTROL_H
#define BM_GRID_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_power.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 命令状态位：使能并网调制输出 */
#define BM_GRID_CTRL_CMD_ENABLED  (1u << 0u)
/** @brief 命令状态位：外部故障锁定 */
#define BM_GRID_CTRL_CMD_FAULT    (1u << 1u)

/** @brief 遥测状态位：数据有效 */
#define BM_GRID_CTRL_TEL_VALID (1u << 0u)
/** @brief 遥测状态位：采样陈旧（历史兼容；读失败现同时锁故障） */
#define BM_GRID_CTRL_TEL_STALE (1u << 1u)
/** @brief 遥测状态位：当前帧处于故障态 */
#define BM_GRID_CTRL_TEL_FAULT (1u << 2u)

/**
 * @brief 并网控制命令
 */
typedef struct {
    uint32_t sequence; /**< 命令序列号 */
    uint32_t status;   /**< 命令状态位（BM_GRID_CTRL_CMD_* 位域） */
} bm_grid_ctrl_cmd_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    theta_rad;
    float    omega_rad_s;
    float    i_ref_a;
    float    i_meas_a;
    float    v_cmd;
} bm_grid_control_telemetry_t;

typedef int (*bm_grid_control_read_fn)(void *user,
                                       float *v_grid,
                                       float *i_meas,
                                       float *i_ref);

typedef int (*bm_grid_control_write_fn)(void *user, float v_cmd);

typedef void (*bm_grid_control_publish_fn)(
    void *user,
    const bm_grid_control_telemetry_t *telemetry);

/**
 * @brief 命令读取回调函数类型
 *
 * @param user    用户上下文指针
 * @param command 输出：最新命令
 * @return 0 成功；非零 无新命令
 */
typedef int (*bm_grid_control_read_command_fn)(void *user,
                                               bm_grid_ctrl_cmd_t *command);

typedef struct {
    bm_grid_control_read_fn         read_io;           /**< 可为 NULL；未绑定按零值继续 */
    void                           *read_io_user;
    bm_grid_control_write_fn        write_output;
    void                           *write_output_user;
    bm_grid_control_publish_fn      publish_telemetry;
    void                           *publish_telemetry_user;
    bm_grid_control_read_command_fn read_command;      /**< 可为 NULL */
    void                           *read_command_user;
} bm_grid_control_resources_t;

typedef struct {
    bm_algo_sogi_pll_config_t pll;
    bm_algo_pr_config_t       pr_current;
    float                     dt_s;
} bm_grid_control_config_t;

typedef struct {
    bm_algo_sogi_pll_state_t pll;
    bm_algo_pr_state_t       pr_current;
    float                    pr_b0, pr_b1, pr_b2, pr_a1, pr_a2;
    float                    theta_rad;
    float                    omega_rad_s;
    float                    v_cmd;
    uint32_t                 step_count;
    bm_grid_ctrl_cmd_t       cmd;           /**< 最新控制命令 */
    int                      fault_latched; /**< 非零：故障已锁存；仅 reset 可清除 */
    bm_grid_control_telemetry_t telemetry;
} bm_grid_control_state_t;

typedef struct {
    bm_grid_control_config_t    config;
    bm_grid_control_resources_t resources;
    bm_grid_control_state_t     state;
} bm_grid_control_axis_t;

/**
 * @brief 校验并网控制配置合法性
 *
 * 检查 dt_s、nominal_omega_rad_s、k_sogi、k_pll、PR kp/kr 及谐振频率。
 *
 * @param config 配置指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_grid_control_validate_config(const bm_grid_control_config_t *config);

/**
 * @brief 初始化并网控制轴（校验 + PR 系数计算 + 复位）
 *
 * @param axis 控制轴指针（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或 PR 系数计算失败
 */
int  bm_grid_control_init(bm_grid_control_axis_t *axis);

/**
 * @brief 复位所有运行状态
 *
 * 清零输出与控制器，清除故障锁存与命令（默认未使能）。
 *
 * @param axis 控制轴指针；NULL 时静默返回
 */
void bm_grid_control_reset(bm_grid_control_axis_t *axis);

/**
 * @brief 应用外部控制命令（使能/故障）
 *
 * 若命令携带 FAULT 位则立即锁存故障并清零 v_cmd。
 *
 * @param axis 实例指针
 * @param cmd  待应用的命令，不可为 NULL
 */
void bm_grid_control_apply_command(bm_grid_control_axis_t *axis,
                                   const bm_grid_ctrl_cmd_t *cmd);

/**
 * @brief 执行一拍 SOGI-PLL + PR 电流环控制
 *
 * 先 sync_command；故障或未 ENABLED 时停止调制并复位控制器。
 * read_io 未绑定则按零值继续；读失败时锁存故障并发布 FAULT/STALE 遥测。
 * 故障锁存仅能经 reset 清除。
 *
 * @param axis 控制轴指针；NULL 时静默返回
 */
void bm_grid_control_step(bm_grid_control_axis_t *axis);

/**
 * @brief exec_ops 调度入口（instance->state 转发至 step）
 *
 * @param instance bm_exec_t 实例（state 指向 bm_grid_control_axis_t）
 */
void bm_grid_control_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并初始化 PR 系数
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_grid_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK
 */
int  bm_grid_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清零 v_cmd 并写入硬件
 *
 * @param instance bm_exec_t 实例
 */
void bm_grid_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief grid_control exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_grid_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_GRID_CONTROL_H */

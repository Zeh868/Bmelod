/**
 * @file process_control.h
 * @brief 过程控制：Smith 预估器 + PID 串级骨架
 *
 * 封装 Smith 预估器与外环/内环 PID 串级控制，
 * 提供 bm_exec_ops_t 调度接口。
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
 * 2026-06-23       0.2            zeh            补 exec_ops 声明；validate_config 增加 Smith 参数校验
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_PROCESS_CONTROL_H
#define BM_PROCESS_CONTROL_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_PROCESS_CTRL_TEL_VALID (1u << 0u)
#define BM_PROCESS_CTRL_TEL_STALE (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    setpoint;
    float    measurement;
    float    outer_out;
    float    inner_out;
} bm_process_control_telemetry_t;

typedef int (*bm_process_control_read_fn)(void *user,
                                          float *setpoint,
                                          float *measurement);

typedef int (*bm_process_control_write_fn)(void *user, float output);

typedef void (*bm_process_control_publish_fn)(
    void *user,
    const bm_process_control_telemetry_t *telemetry);

typedef struct {
    bm_process_control_read_fn    read_io;
    void                         *read_io_user;
    bm_process_control_write_fn   write_output;
    void                         *write_output_user;
    bm_process_control_publish_fn publish_telemetry;
    void                         *publish_telemetry_user;
} bm_process_control_resources_t;

typedef struct {
    bm_algo_pid_config_t              outer_pid;
    bm_algo_pid_config_t              inner_pid;
    bm_algo_smith_predictor_config_t  smith;
    float                            *smith_delay_line;
    uint32_t                          smith_line_len;
    float                             dt_s;
} bm_process_control_config_t;

typedef struct {
    bm_algo_pid_state_t              outer_pid;
    bm_algo_pid_state_t              inner_pid;
    bm_algo_smith_predictor_state_t  smith;
    float                            outer_out;
    float                            inner_out;
    uint32_t                         step_count;
    bm_process_control_telemetry_t   telemetry;
} bm_process_control_state_t;

typedef struct {
    bm_process_control_config_t    config;
    bm_process_control_resources_t resources;
    bm_process_control_state_t     state;
} bm_process_control_axis_t;

/**
 * @brief 校验过程控制配置合法性
 *
 * 检查 dt_s、Smith 预估器 model_gain/delay_steps 及延迟线缓冲区。
 *
 * @param config 配置指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_process_control_validate_config(const bm_process_control_config_t *config);

/**
 * @brief 初始化过程控制轴（校验 + Smith 初始化 + 复位）
 *
 * @param axis 控制轴指针（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或 Smith 初始化失败
 */
int  bm_process_control_init(bm_process_control_axis_t *axis);

/**
 * @brief 复位所有运行状态
 *
 * @param axis 控制轴指针（不可为 NULL）
 */
void bm_process_control_reset(bm_process_control_axis_t *axis);

/**
 * @brief 执行一拍 Smith 预估 + PID 串级控制
 *
 * 从 read_io 读取设定值与测量值，Smith 预估补偿延迟，
 * 串级 PID 计算输出，写入 write_output 并发布遥测。
 *
 * @param axis 控制轴指针（不可为 NULL）
 */
void bm_process_control_step(bm_process_control_axis_t *axis);

/**
 * @brief exec_ops 调度入口（instance->state 转发至 step）
 *
 * @param instance bm_exec_t 实例（state 指向 bm_process_control_axis_t）
 */
void bm_process_control_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并初始化 Smith 预估器
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_process_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK
 */
int  bm_process_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清零输出并写入硬件
 *
 * @param instance bm_exec_t 实例
 */
void bm_process_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief process_control exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_process_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_PROCESS_CONTROL_H */

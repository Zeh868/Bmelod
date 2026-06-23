/**
 * @file control_loop.h
 * @brief 串级 PI 控制环骨架（外环设定 → 内环跟踪）
 *
 * 外环输出作为内环设定；饱和与抗饱和由 bm_algo_pi 承担。
 * 提供 bm_exec_ops_t 标准封装，可直接接入调度框架。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始 K1 骨架
 * 2026-06-23       0.2            zeh            补 bm_exec_ops_t 标准调度封装接口
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_CONTROL_LOOP_H
#define BM_CONTROL_LOOP_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct {
    bm_control_loop_read_plant_fn read_plant;
    void                           *read_plant_user;
    bm_control_loop_write_output_fn write_output;
    void                           *write_output_user;
} bm_control_loop_resources_t;

typedef struct {
    bm_algo_pi_state_t outer_pi;
    bm_algo_pi_state_t inner_pi;
    float              outer_out;
    float              inner_out;
    uint32_t           step_count;
} bm_control_loop_state_t;

typedef struct {
    bm_control_loop_config_t    config;
    bm_control_loop_resources_t resources;
    bm_control_loop_state_t     state;
} bm_control_loop_axis_t;

int  bm_control_loop_validate_config(const bm_control_loop_config_t *config);
void bm_control_loop_reset(bm_control_loop_axis_t *axis);
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

/**
 * @file additive_motion.h
 * @brief 增材 Z 轴输入整形（ZV 两脉冲 E1）
 *
 * 对 Z 轴位置指令施加零振动整形，降低共振激励；提供
 * exec_ops 表供 bm_exec 周期调度框架接入。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            ZV 两脉冲骨架
 * 2026-06-17       0.2            zeh            pressure advance 线性模型
 * 2026-06-23       0.3            zeh            exec_ops 表；static 辅助函数 Doxygen；SPDX
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ADDITIVE_MOTION_H
#define BM_ADDITIVE_MOTION_H

#include "bm/hybrid/bm_exec.h"

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

/**
 * @brief 校验增材运动配置合法性
 *
 * 检查 dt_s > 0、natural_freq_hz > 0、max_velocity_mm_s > 0。
 *
 * @param config 待校验配置指针，不得为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数非法
 */
int  bm_additive_motion_validate_config(const bm_additive_motion_config_t *config);

/**
 * @brief 初始化增材运动轴
 *
 * 校验配置并执行 reset（含 ZV 系数计算）。
 *
 * @param axis 轴实例指针，不得为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int  bm_additive_motion_init(bm_additive_motion_axis_t *axis);

/**
 * @brief 复位增材运动状态
 *
 * 清零环形缓冲区、shaped_mm、step_count 并重新计算 ZV 系数。
 *
 * @param axis 轴实例指针；NULL 时直接返回
 */
void bm_additive_motion_reset(bm_additive_motion_axis_t *axis);

/**
 * @brief 对位置指令施加 ZV 两脉冲输入整形
 *
 * 每次调用更新环形缓冲区，输出累积整形位置写入
 * axis->state.shaped_mm，同时更新遥测中的 raw/shaped 字段。
 *
 * @param axis    轴实例指针；NULL 时直接返回
 * @param cmd_mm  当前目标位置（mm）
 */
void bm_additive_motion_shape_cmd(bm_additive_motion_axis_t *axis,
                                  float cmd_mm);

/**
 * @brief 执行一次周期步进（速度限幅 + 写出 + 遥测发布）
 *
 * @param axis 轴实例指针；NULL 或配置无效时直接返回
 */
void bm_additive_motion_step(bm_additive_motion_axis_t *axis);

/**
 * @brief E1 线性挤出超前补偿
 *
 * extrusion_mm = velocity_mm_s × factor
 *
 * @param velocity_mm_s 当前速度（mm/s）
 * @param factor        超前系数（s）
 * @return 超前补偿量（mm）
 */
float bm_additive_motion_pressure_advance(float velocity_mm_s, float factor);

/* ---------- exec_ops 接口（供 bm_exec 周期调度） ---------- */

/**
 * @brief exec_ops init 回调：校验配置并执行 reset
 *
 * @param instance bm_exec 实例指针；instance->state 须指向
 *                 bm_additive_motion_axis_t
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int  bm_additive_motion_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前为空操作）
 *
 * @param instance bm_exec 实例指针
 * @return BM_OK
 */
int  bm_additive_motion_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清零整形输出并复位状态
 *
 * @param instance bm_exec 实例指针；instance->state 须指向
 *                 bm_additive_motion_axis_t
 */
void bm_additive_motion_exec_safe_stop(const bm_exec_t *instance);

/**
 * @brief exec 周期运行回调：调用 bm_additive_motion_step
 *
 * @param instance bm_exec 实例指针；instance->state 须指向
 *                 bm_additive_motion_axis_t
 */
void bm_additive_motion_exec_run(const bm_exec_t *instance);

/** exec_ops 表，供 bm_exec_t.ops 字段引用 */
extern const bm_exec_ops_t bm_additive_motion_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_ADDITIVE_MOTION_H */

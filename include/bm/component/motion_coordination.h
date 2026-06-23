/**
 * @file motion_coordination.h
 * @brief 多轴斜坡/轨迹协调组件
 *
 * 最多支持 BM_MOTION_COORD_MAX_AXES（4）轴同步斜坡协调。
 * 每轴维护独立的 bm_algo_ramp_state_t，在 step 中同步推进到
 * 各自的 target。
 *
 * exec_ops 接入：
 *   bm_motion_coordination_exec_ops 为周期 step 组件提供标准生命周期表，
 *   调用方须将 bm_exec_t.state 指向 bm_motion_coordination_axis_t。
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
 * 2026-06-23       0.2            zeh            补 exec_ops 封装；Doxygen；SPDX
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_MOTION_COORDINATION_H
#define BM_MOTION_COORDINATION_H

#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_MOTION_COORD_MAX_AXES 4u
#define BM_MOTION_COORD_TEL_VALID (1u << 0u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    uint32_t axis_count;
    float    position[BM_MOTION_COORD_MAX_AXES];
} bm_motion_coordination_telemetry_t;

typedef void (*bm_motion_coordination_publish_fn)(
    void *user,
    const bm_motion_coordination_telemetry_t *telemetry);

typedef struct {
    bm_motion_coordination_publish_fn publish_telemetry;
    void                             *publish_telemetry_user;
} bm_motion_coordination_resources_t;

typedef struct {
    uint32_t              axis_count;
    bm_algo_ramp_config_t ramp[BM_MOTION_COORD_MAX_AXES];
    float                 dt_s;
} bm_motion_coordination_config_t;

typedef struct {
    bm_algo_ramp_state_t ramp[BM_MOTION_COORD_MAX_AXES];
    float                target[BM_MOTION_COORD_MAX_AXES];
    uint32_t             step_count;
    bm_motion_coordination_telemetry_t telemetry;
} bm_motion_coordination_state_t;

typedef struct {
    bm_motion_coordination_config_t    config;
    bm_motion_coordination_resources_t resources;
    bm_motion_coordination_state_t     state;
} bm_motion_coordination_axis_t;

/**
 * @brief 校验多轴协调配置合法性
 *
 * 检查 dt_s > 0、axis_count ∈ [1, BM_MOTION_COORD_MAX_AXES]，
 * 以及每轴 ramp.rate_per_s > 0。
 *
 * @param config 配置指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_motion_coordination_validate_config(
    const bm_motion_coordination_config_t *config);

/**
 * @brief 初始化多轴协调实例（校验配置 + 全轴复位到 0）
 *
 * @param axis 轴实例指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或 axis 为 NULL
 */
int  bm_motion_coordination_init(bm_motion_coordination_axis_t *axis);

/**
 * @brief 复位各轴斜坡状态到指定初始位置
 *
 * @param axis    轴实例指针，NULL 时静默返回
 * @param initial 各轴初始位置数组（长度须 ≥ axis_count），NULL 时全部复位为 0
 */
void bm_motion_coordination_reset(bm_motion_coordination_axis_t *axis,
                                  const float *initial);

/**
 * @brief 更新各轴目标位置
 *
 * @param axis    轴实例指针，NULL 时静默返回
 * @param targets 各轴目标位置数组（长度须 ≥ axis_count），NULL 时静默返回
 */
void bm_motion_coordination_set_targets(bm_motion_coordination_axis_t *axis,
                                        const float *targets);

/**
 * @brief 周期执行单步：各轴斜坡推进并发布遥测
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_motion_coordination_step(bm_motion_coordination_axis_t *axis);

/* ---------- exec_ops 封装（bm_exec 周期调度接口） ---------- */

/**
 * @brief exec_ops.init 转发：校验配置并复位到全零
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_motion_coordination_axis_t
 * @return BM_OK 成功；BM_ERR_INVALID 参数或配置非法
 */
int  bm_motion_coordination_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops.start 转发：无额外启动动作，始终返回 BM_OK
 *
 * @param instance bm_exec 实例
 * @return BM_OK
 */
int  bm_motion_coordination_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops.safe_stop 转发：将所有轴目标设为当前位置（就地停止）
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_motion_coordination_axis_t
 */
void bm_motion_coordination_exec_safe_stop(const bm_exec_t *instance);

/**
 * @brief exec 周期运行函数：转发至 bm_motion_coordination_step
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_motion_coordination_axis_t
 */
void bm_motion_coordination_exec_run(const bm_exec_t *instance);

/** @brief motion_coordination 标准 exec 生命周期操作表 */
extern const bm_exec_ops_t bm_motion_coordination_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTION_COORDINATION_H */

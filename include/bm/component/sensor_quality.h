/**
 * @file sensor_quality.h
 * @brief 传感器信号质量监控（范围、变化率、冻结值）
 *
 * 本组件对单通道模拟传感器执行三级质量检查：
 *   1. 范围越限（under-range / over-range）；
 *   2. 变化率超限；
 *   3. 冻结值检测（连续 N 拍差值 ≤ frozen_epsilon）。
 * 任意故障均通过遥测的 fault_flags 字段上报，组件本身不执行联锁。
 *
 * exec_ops 接入：
 *   bm_sensor_quality_exec_ops 为周期 step 组件提供标准生命周期表，
 *   调用方须将 bm_exec_t.state 指向 bm_sensor_quality_axis_t。
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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_SENSOR_QUALITY_H
#define BM_SENSOR_QUALITY_H

#include "bm/algorithm/bm_algo_signal_quality.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_SENSOR_QUALITY_TEL_VALID (1u << 0u)
#define BM_SENSOR_QUALITY_TEL_STALE (1u << 1u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    value;
    uint32_t fault_flags;
} bm_sensor_quality_telemetry_t;

typedef int (*bm_sensor_quality_read_sample_fn)(void *user, float *sample);

typedef void (*bm_sensor_quality_publish_fn)(
    void *user,
    const bm_sensor_quality_telemetry_t *telemetry);

typedef struct {
    bm_sensor_quality_read_sample_fn read_sample;
    void                            *read_sample_user;
    bm_sensor_quality_publish_fn     publish_telemetry;
    void                            *publish_telemetry_user;
} bm_sensor_quality_resources_t;

typedef struct {
    bm_algo_range_monitor_config_t monitor;
    float                          frozen_epsilon;
    uint32_t                       frozen_count_required;
    float                          dt_s;
} bm_sensor_quality_config_t;

typedef struct {
    bm_algo_range_monitor_state_t monitor;
    float                         frozen_prev;
    uint32_t                      frozen_count;
    uint32_t                      fault_flags;
    float                         last_value;
    uint32_t                      step_count;
    bm_sensor_quality_telemetry_t telemetry;
} bm_sensor_quality_state_t;

typedef struct {
    bm_sensor_quality_config_t    config;
    bm_sensor_quality_resources_t resources;
    bm_sensor_quality_state_t     state;
} bm_sensor_quality_axis_t;

/**
 * @brief 校验配置参数合法性
 *
 * @param config 配置指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_sensor_quality_validate_config(const bm_sensor_quality_config_t *config);

/**
 * @brief 初始化轴并复位内部状态
 *
 * @param axis    轴实例指针，NULL 时返回 BM_ERR_INVALID
 * @param initial 复位时的初始采样值（用于冻结基准与范围监控起点）
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或 axis 为 NULL
 */
int  bm_sensor_quality_init(bm_sensor_quality_axis_t *axis, float initial);

/**
 * @brief 复位内部状态（不重新校验配置）
 *
 * @param axis    轴实例指针，NULL 时静默返回
 * @param initial 复位时的初始采样值
 */
void bm_sensor_quality_reset(bm_sensor_quality_axis_t *axis, float initial);

/**
 * @brief 周期执行单步：读采样、范围/变化率/冻结检测、发布遥测
 *
 * 若 read_sample 返回非零，则遥测 status 置 BM_SENSOR_QUALITY_TEL_STALE，
 * 保持上一次有效值。
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_sensor_quality_step(bm_sensor_quality_axis_t *axis);

/* ---------- exec_ops 封装（bm_exec 周期调度接口） ---------- */

/**
 * @brief exec_ops.init 转发：校验配置并复位状态
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_sensor_quality_axis_t
 * @return BM_OK 成功；BM_ERR_INVALID instance 或 state 为 NULL，或配置非法
 */
int  bm_sensor_quality_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops.start 转发：当前无额外启动动作，始终返回 BM_OK
 *
 * @param instance bm_exec 实例
 * @return BM_OK
 */
int  bm_sensor_quality_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops.safe_stop 转发：清零 fault_flags 并停止输出
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_sensor_quality_axis_t
 */
void bm_sensor_quality_exec_safe_stop(const bm_exec_t *instance);

/**
 * @brief exec 周期运行函数：转发至 bm_sensor_quality_step
 *
 * @param instance bm_exec 实例；instance->state 须指向 bm_sensor_quality_axis_t
 */
void bm_sensor_quality_exec_run(const bm_exec_t *instance);

/** @brief sensor_quality 标准 exec 生命周期操作表 */
extern const bm_exec_ops_t bm_sensor_quality_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_SENSOR_QUALITY_H */

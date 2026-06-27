/**
 * @file solar_control.h
 * @brief 光伏 MPPT 编排与限功率骨架
 *
 * 封装 P&O/增量电导 MPPT 与功率限额降额，输出工作点参考。
 * 提供 bm_exec_ops_t 接口，可直接挂入框架调度器。
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
 * 2026-06-23       0.2            zeh            补 exec_ops 封装声明；validate_config 字段校验
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_SOLAR_CONTROL_H
#define BM_SOLAR_CONTROL_H

#include "bm/algorithm/bm_algo_power.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_SOLAR_CTRL_TEL_VALID     (1u << 0u)
#define BM_SOLAR_CTRL_TEL_LIMITED   (1u << 1u)
#define BM_SOLAR_CTRL_TEL_STALE     (1u << 2u)

typedef enum {
    BM_SOLAR_MPPT_PO = 0,
    BM_SOLAR_MPPT_IC
} bm_solar_mppt_mode_t;

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    voltage_v;
    float    current_a;
    float    power_w;
    float    v_ref_v;
} bm_solar_control_telemetry_t;

typedef int (*bm_solar_read_iv_fn)(void *user,
                                   float *voltage_v,
                                   float *current_a);

typedef int (*bm_solar_write_vref_fn)(void *user, float v_ref_v);

typedef void (*bm_solar_publish_fn)(void *user,
                                    const bm_solar_control_telemetry_t *telemetry);

typedef struct {
    bm_solar_read_iv_fn    read_iv;
    void                  *read_iv_user;
    bm_solar_write_vref_fn write_vref;
    void                  *write_vref_user;
    bm_solar_publish_fn    publish_telemetry;
    void                  *publish_telemetry_user;
} bm_solar_control_resources_t;

typedef struct {
    bm_solar_mppt_mode_t       mppt_mode;
    bm_algo_mppt_po_config_t   mppt_po;
    bm_algo_mppt_ic_config_t   mppt_ic;
    float                      power_limit_w;
    float                      v_init_v;
} bm_solar_control_config_t;

typedef struct {
    bm_algo_mppt_po_state_t po;
    bm_algo_mppt_ic_state_t ic;
    float v_ref_v;
    float last_power_w;
    uint32_t step_count;
    bm_solar_control_telemetry_t telemetry;
} bm_solar_control_state_t;

typedef struct {
    bm_solar_control_config_t    config;
    bm_solar_control_resources_t resources;
    bm_solar_control_state_t     state;
} bm_solar_control_axis_t;

/**
 * @brief 校验配置合法性
 *
 * 检查 v_init_v、step_v、power_limit_w 及 MPPT 参数边界。
 *
 * @param config 配置指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_solar_control_validate_config(const bm_solar_control_config_t *config);

/**
 * @brief 初始化控制轴（校验 + 复位）
 *
 * @param axis 控制轴指针（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法
 */
int  bm_solar_control_init(bm_solar_control_axis_t *axis);

/**
 * @brief 复位所有运行状态
 *
 * @param axis 控制轴指针（不可为 NULL）
 */
void bm_solar_control_reset(bm_solar_control_axis_t *axis);

/**
 * @brief 执行一拍 MPPT 步进并处理功率限额降额
 *
 * 从 read_iv 读取 IV，执行 P&O 或 IC 算法，限功率后写入 write_vref，
 * 并通过 publish_telemetry 发布遥测。
 *
 * @param axis 控制轴指针（不可为 NULL）
 */
void bm_solar_control_step(bm_solar_control_axis_t *axis);

/**
 * @brief exec_ops 调度入口（instance->state 转发至 step）
 *
 * @param instance bm_exec_t 实例指针（state 字段须指向 bm_solar_control_axis_t）
 */
void bm_solar_control_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并复位状态
 *
 * @param instance bm_exec_t 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_solar_control_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例指针
 * @return BM_OK
 */
int  bm_solar_control_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：清零 v_ref 并写入硬件
 *
 * @param instance bm_exec_t 实例指针
 */
void bm_solar_control_exec_safe_stop(const bm_exec_t *instance);

/** @brief solar_control exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_solar_control_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_SOLAR_CONTROL_H */

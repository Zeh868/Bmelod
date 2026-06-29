/**
 * @file bms_supervision.h
 * @brief BMS Pack 监督：限值检查与 fault_derating 集成
 *
 * 封装电压/电流/温度越限检测，驱动 fault_derating 组件进行降额，
 * 并提供 bm_exec_ops_t 调度封装以接入 bm_exec 生命周期管理。
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
 * 2026-06-23       0.2            zeh            补 exec_ops 声明；补全公共函数 Doxygen；SPDX 头
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_BMS_SUPERVISION_H
#define BM_BMS_SUPERVISION_H

#include "bm/component/fault_derating.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_BMS_SUP_TEL_VALID    (1u << 0u)
#define BM_BMS_SUP_TEL_DERATED  (1u << 1u)
#define BM_BMS_SUP_TEL_STALE    (1u << 2u)

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    pack_voltage_v;
    float    pack_current_a;
    float    temp_c;
    float    derate_factor;
    uint32_t limit_flags;
} bm_bms_supervision_telemetry_t;

typedef int (*bm_bms_supervision_read_sample_fn)(void *user,
                                                 float *voltage_v,
                                                 float *current_a,
                                                 float *temp_c);

typedef void (*bm_bms_supervision_publish_fn)(
    void *user,
    const bm_bms_supervision_telemetry_t *telemetry);

typedef struct {
    bm_bms_supervision_read_sample_fn read_sample;
    void                             *read_sample_user;
    bm_bms_supervision_publish_fn     publish_telemetry;
    void                             *publish_telemetry_user;
} bm_bms_supervision_resources_t;

#define BM_BMS_SUP_LIMIT_VOLTAGE_HIGH (1u << 0u)
#define BM_BMS_SUP_LIMIT_VOLTAGE_LOW  (1u << 1u)
#define BM_BMS_SUP_LIMIT_CURRENT      (1u << 2u)
#define BM_BMS_SUP_LIMIT_TEMP         (1u << 3u)

typedef struct {
    float v_max_v;
    float v_min_v;
    float i_max_a;
    float temp_max_c;
    float dt_s;
    bm_algo_ramp_config_t derate_ramp;
    float                 recovery_time_s;
    float                 derate_target;
} bm_bms_supervision_config_t;

typedef struct {
    bm_fault_derating_axis_t derating;
    uint32_t                 limit_flags;
    float                    pack_voltage_v;
    float                    pack_current_a;
    float                    temp_c;
    uint32_t                 step_count;
    bm_bms_supervision_telemetry_t telemetry;
} bm_bms_supervision_state_t;

typedef struct {
    bm_bms_supervision_config_t    config;
    bm_bms_supervision_resources_t resources;
    bm_bms_supervision_state_t     state;
} bm_bms_supervision_axis_t;

/**
 * @brief 校验 BMS 监督配置合法性
 *
 * 检查 dt_s > 0、v_max_v > v_min_v、i_max_a > 0、降额斜坡率 > 0、
 * recovery_time_s >= 0 以及 derate_target 在 [0, 1] 范围内。
 *
 * @param config 配置结构体只读指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段非法
 */
int  bm_bms_supervision_validate_config(const bm_bms_supervision_config_t *config);

/**
 * @brief 初始化 BMS 监督轴（校验配置、同步降额参数、复位状态）
 *
 * @param axis 轴实例指针，NULL 或配置非法时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 参数或配置非法
 */
int  bm_bms_supervision_init(bm_bms_supervision_axis_t *axis);

/**
 * @brief 复位 BMS 监督轴运行状态（不修改 config/resources）
 *
 * 复位 fault_derating 子组件，清零所有状态字段。
 *
 * @param axis 轴实例指针；为 NULL 时静默返回
 */
void bm_bms_supervision_reset(bm_bms_supervision_axis_t *axis);

/**
 * @brief 执行一个控制周期的 BMS 监督步进
 *
 * 从 resources.read_sample 读取电压/电流/温度，检测越限标志，
 * 驱动 fault_derating 降额，更新遥测并通过 publish_telemetry 上报。
 * read_sample 失败时打 STALE 标志并直接发布上一拍快照。
 *
 * @param axis 指向已初始化的轴实例；为 NULL 时静默返回
 */
void bm_bms_supervision_step(bm_bms_supervision_axis_t *axis);

/**
 * @brief exec_ops run 回调：将 instance->state 转发至 bm_bms_supervision_step
 *
 * @param instance bm_exec_t 实例（state 指向 bm_bms_supervision_axis_t）
 */
void bm_bms_supervision_exec_run(const bm_exec_t *instance);

/**
 * @brief exec_ops init 回调：校验配置并完成初始化
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为 NULL
 */
int  bm_bms_supervision_exec_init(const bm_exec_t *instance);

/**
 * @brief exec_ops start 回调（当前无需额外操作）
 *
 * @param instance bm_exec_t 实例
 * @return BM_OK
 */
int  bm_bms_supervision_exec_start(const bm_exec_t *instance);

/**
 * @brief exec_ops safe_stop 回调：复位降额因子，发布最终遥测
 *
 * @param instance bm_exec_t 实例
 */
void bm_bms_supervision_exec_safe_stop(const bm_exec_t *instance);

/** @brief bms_supervision exec_ops 表，可直接赋给 bm_exec_t.ops */
extern const bm_exec_ops_t bm_bms_supervision_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_BMS_SUPERVISION_H */

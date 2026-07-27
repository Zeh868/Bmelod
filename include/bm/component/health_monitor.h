/**
 * @file health_monitor.h
 * @brief 系统健康聚合组件：收集各故障源的统一故障码，维护系统级健康快照
 *
 * 事件驱动（无周期 step、不提供 exec_ops）：应用或各诊断组件的遥测
 * 回调在检测到故障变化时调用 bm_health_monitor_report() 上报统一故障码
 * （见 bm/component/bm_fault.h），组件据此维护每个故障源的活动/锁存状态
 * 与全系统最严重严重度；系统级状态发生变化时发布一次遥测。
 *
 * 故障源表由用户静态分配（无 malloc），每个表项以 source_id 标识；
 * E1 阶段按 source_id 线性查表，源数量由应用控制。
 *
 * 接线示例（sensor_quality → health_monitor）：
 * @code
 * static void on_sensor_tel(void *user,
 *                           const bm_sensor_quality_telemetry_t *tel) {
 *     bm_health_monitor_t *mon = (bm_health_monitor_t *)user;
 *     bm_fault_code_t code = BM_FAULT_NONE;
 *     if (tel->fault_flags & BM_ALGO_FAULT_FROZEN) {
 *         code = BM_FAULT_SENSOR_FROZEN;
 *     } else if (tel->fault_flags & (BM_ALGO_FAULT_UNDER_RANGE |
 *                                    BM_ALGO_FAULT_OVER_RANGE)) {
 *         code = BM_FAULT_SENSOR_OVER_RANGE;
 *     }
 *     bm_health_monitor_report(mon, SOURCE_ID_CURRENT_SENSOR, code,
 *                              code == BM_FAULT_NONE ? BM_FAULT_SEVERITY_NONE
 *                                                    : BM_FAULT_SEVERITY_ERROR);
 * }
 * @endcode
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       0.1            zeh            初始版本
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_HEALTH_MONITOR_H
#define BM_HEALTH_MONITOR_H

#include "bm/component/bm_fault.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 遥测 status 位：遥测数据有效 */
#define BM_HEALTH_MONITOR_TEL_VALID (1u << 0u)

/**
 * @brief 故障源表项（数组由用户提供，init 时整体复位）
 */
typedef struct {
    uint16_t            source_id;      /**< 应用分配的源 ID（表内唯一，用户填写） */
    bm_fault_code_t     active_code;    /**< 当前活动故障码，BM_FAULT_NONE 表示无故障 */
    bm_fault_code_t     latched_code;   /**< 历史锁存故障码（reset 才清除） */
    uint8_t             severity;       /**< 当前活动严重度，见 bm_fault_severity_t */
    uint8_t             worst_severity; /**< 历史最严重严重度（reset 才清除） */
    uint32_t            report_count;   /**< 累计上报次数 */
} bm_health_monitor_source_t;

/**
 * @brief 系统级健康快照（遥测负载，也是 system_health 查询输出）
 */
typedef struct {
    uint32_t        sequence;        /**< 单调递增序列号（每次发布 +1） */
    uint32_t        status;          /**< 状态位掩码，见 BM_HEALTH_MONITOR_TEL_* */
    uint8_t         worst_severity;  /**< 全系统当前活动故障中最严重严重度 */
    uint8_t         sources_active;  /**< 当前有活动故障的源数量 */
    uint16_t        sources_latched; /**< 有锁存记录的源数量 */
    uint16_t        active_source_id; /**< 当前最重活动故障的源 ID，无活动故障时为 0 */
    bm_fault_code_t active_code;     /**< 当前最重活动故障的故障码，无活动故障时为 BM_FAULT_NONE */
} bm_health_monitor_telemetry_t;

/**
 * @brief 遥测发布回调函数原型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前健康快照（const，生命周期仅在回调内有效）
 */
typedef void (*bm_health_monitor_publish_fn)(
    void *user,
    const bm_health_monitor_telemetry_t *telemetry);

/**
 * @brief 健康监视器外部资源绑定
 */
typedef struct {
    bm_health_monitor_publish_fn publish_telemetry;      /**< 遥测发布回调，NULL 时不发布 */
    void                        *publish_telemetry_user; /**< 遥测回调用户上下文 */
} bm_health_monitor_resources_t;

/**
 * @brief 健康监视器配置
 */
typedef struct {
    bm_health_monitor_source_t *sources;      /**< 用户提供的故障源表（每项 source_id 须已填写） */
    uint32_t                    source_count; /**< 表项数量，须 > 0 */
} bm_health_monitor_config_t;

/**
 * @brief 健康监视器运行状态
 */
typedef struct {
    uint32_t                      report_seq; /**< 遥测序列号计数 */
    bm_health_monitor_telemetry_t telemetry;  /**< 最新系统健康快照 */
} bm_health_monitor_state_t;

/**
 * @brief 健康监视器聚合对象
 */
typedef struct {
    bm_health_monitor_config_t    config;    /**< 配置（用户填写） */
    bm_health_monitor_resources_t resources; /**< 外部资源绑定 */
    bm_health_monitor_state_t     state;     /**< 运行状态（由组件维护） */
} bm_health_monitor_t;

/**
 * @brief 校验健康监视器配置合法性
 *
 * @param config 配置结构体指针（const），NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段不合法
 */
int bm_health_monitor_validate_config(const bm_health_monitor_config_t *config);

/**
 * @brief 初始化健康监视器（复位所有故障源表项的运行字段，保留 source_id）
 *
 * @param mon 监视器实例指针，NULL 或配置非法时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 参数/配置非法
 */
int bm_health_monitor_init(bm_health_monitor_t *mon);

/**
 * @brief 复位健康监视器：清空所有源的活动/锁存状态与遥测快照
 *
 * @param mon 监视器实例指针，NULL 时静默返回
 */
void bm_health_monitor_reset(bm_health_monitor_t *mon);

/**
 * @brief 上报某故障源的当前故障码
 *
 * code 为 BM_FAULT_NONE 表示该源故障已清除：清 active_code/severity，
 * 保留 latched_code/worst_severity；非 NONE 时更新活动状态，若严重度
 * 超过历史值则刷新 worst_severity，并锁存该故障码。
 * 系统级健康快照较上一次发布有变化时，发布一次遥测。
 *
 * @param mon       监视器实例指针，NULL 时返回 BM_ERR_INVALID
 * @param source_id 故障源 ID（须已在源表中注册）
 * @param code      统一故障码（bm_fault_code_t），BM_FAULT_NONE 表示清除
 * @param severity  本次故障严重度；code 为 BM_FAULT_NONE 时忽略
 * @return BM_OK 成功；BM_ERR_INVALID mon 为 NULL 或 source_id 未注册
 */
int bm_health_monitor_report(bm_health_monitor_t *mon,
                             uint16_t source_id,
                             bm_fault_code_t code,
                             bm_fault_severity_t severity);

/**
 * @brief 查询当前系统级健康快照
 *
 * @param mon  监视器实例指针（const）
 * @param out  输出快照指针
 * @return BM_OK 成功；BM_ERR_INVALID 任一参数为 NULL
 */
int bm_health_monitor_system_health(const bm_health_monitor_t *mon,
                                    bm_health_monitor_telemetry_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BM_HEALTH_MONITOR_H */

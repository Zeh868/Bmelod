/**
 * @file fault_derating.h
 * @brief 故障锁存、线性降额曲线与恢复定时器领域组件
 *
 * 提供单轴故障降额逻辑：检测到故障后沿斜坡将 derate_factor 从 1.0
 * 降至 derate_target；故障清除后等待 recovery_time_s 再沿斜坡恢复到 1.0。
 * 每步更新遥测并可选发布到外部回调。
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
 * 2026-06-23       0.2            zeh            补全 Doxygen 中文注释；添加 SPDX 头
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_FAULT_DERATING_H
#define BM_FAULT_DERATING_H

#include "bm/algorithm/bm_algo_profile.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 遥测 status 位：遥测数据有效 */
#define BM_FAULT_DERATING_TEL_VALID   (1u << 0u)
/** @brief 遥测 status 位：当前处于故障锁存状态 */
#define BM_FAULT_DERATING_TEL_LATCHED (1u << 1u)

/**
 * @brief 故障降额遥测快照
 */
typedef struct {
    uint32_t sequence;           /**< 单调递增序列号（每步 +1） */
    uint32_t status;             /**< 状态位掩码，见 BM_FAULT_DERATING_TEL_* */
    float    derate_factor;      /**< 当前降额因子，范围 [0.0, 1.0] */
    float    recovery_elapsed_s; /**< 故障清除后已累计的恢复等待时间（秒） */
} bm_fault_derating_telemetry_t;

/**
 * @brief 遥测发布回调函数原型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前遥测快照（const，生命周期仅在回调内有效）
 */
typedef void (*bm_fault_derating_publish_fn)(
    void *user,
    const bm_fault_derating_telemetry_t *telemetry);

/**
 * @brief 故障降额轴外部资源绑定
 */
typedef struct {
    bm_fault_derating_publish_fn publish_telemetry;      /**< 遥测发布回调，NULL 时不发布 */
    void                        *publish_telemetry_user; /**< 遥测回调用户上下文 */
} bm_fault_derating_resources_t;

/**
 * @brief 故障降额轴配置
 */
typedef struct {
    bm_algo_ramp_config_t derate_ramp;     /**< 降额斜坡配置（变化率，单位/秒） */
    float                 recovery_time_s; /**< 故障清除后等待恢复的时长（秒），须 >= 0 */
    float                 dt_s;            /**< 控制周期时间步长（秒），须 > 0 */
    float                 derate_target;   /**< 降额目标因子，范围 [0.0, 1.0] */
} bm_fault_derating_config_t;

/**
 * @brief 故障降额轴运行状态
 */
typedef struct {
    bm_algo_ramp_state_t derate_ramp;        /**< 降额斜坡运行状态 */
    int                  fault_latched;      /**< 非零表示故障已锁存 */
    float                derate_factor;      /**< 当前输出降额因子，范围 [0.0, 1.0] */
    float                recovery_elapsed_s; /**< 故障清除后已累计的恢复等待时间（秒） */
    uint32_t             step_count;         /**< 总步计数 */
    bm_fault_derating_telemetry_t telemetry; /**< 最新遥测快照 */
} bm_fault_derating_state_t;

/**
 * @brief 故障降额轴聚合对象
 */
typedef struct {
    bm_fault_derating_config_t    config;    /**< 轴配置（用户填写） */
    bm_fault_derating_resources_t resources; /**< 外部资源绑定 */
    bm_fault_derating_state_t     state;     /**< 运行状态（由组件维护） */
} bm_fault_derating_axis_t;

/**
 * @brief 校验故障降额配置合法性
 *
 * @param config 配置结构体指针（const），NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 任一字段不合法
 */
int  bm_fault_derating_validate_config(const bm_fault_derating_config_t *config);

/**
 * @brief 初始化故障降额轴
 *
 * @param axis 轴实例指针，NULL 或配置非法时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 参数/配置非法
 */
int  bm_fault_derating_init(bm_fault_derating_axis_t *axis);

/**
 * @brief 复位故障降额轴至全额状态（derate_factor = 1.0）
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_reset(bm_fault_derating_axis_t *axis);

/**
 * @brief 锁存故障，触发降额斜坡
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_latch(bm_fault_derating_axis_t *axis);

/**
 * @brief 请求清除故障锁存，重新启动恢复计时器
 *
 * 仅在 fault_latched 为非零时生效；axis 为 NULL 或未锁存时静默返回。
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_clear_request(bm_fault_derating_axis_t *axis);

/**
 * @brief 故障降额单步更新
 *
 * 每个控制周期调用一次；驱动降额/恢复斜坡并更新遥测。
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_fault_derating_step(bm_fault_derating_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_FAULT_DERATING_H */

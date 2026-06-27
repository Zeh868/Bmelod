/**
 * @file bms_estimation.h
 * @brief BMS Pack 估算领域组件（库仑 + OCV 融合 / SOC EKF + 温度补偿）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            增加 SOC EKF 模式
 * 2026-06-23       0.3            zeh            补 SPDX 与函数级 Doxygen
 *
 * 对齐路线图 §5.3 pack_estimator：低频 SOC 估算，无 HAL 硬编码。
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_BMS_ESTIMATION_H
#define BM_BMS_ESTIMATION_H

#include "bm/algorithm/bm_algo_battery.h"
#include "bm/algorithm/bm_algo_battery_model.h"
#include "bm/hybrid/bm_exec.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 遥测状态位：数据有效 */
#define BM_BMS_EST_TEL_VALID  (1u << 0u)
/** @brief 遥测状态位：采样超时（数据陈旧） */
#define BM_BMS_EST_TEL_STALE  (1u << 1u)
/** @brief 遥测状态位：当前帧使用 EKF 模式 */
#define BM_BMS_EST_TEL_EKF    (1u << 2u)

/**
 * @brief SOC 估算模式枚举
 */
typedef enum {
    BM_BMS_EST_MODE_FUSION = 0, /**< 库仑计数 + OCV 融合（默认） */
    BM_BMS_EST_MODE_EKF    = 1  /**< SOC 扩展卡尔曼滤波 */
} bm_bms_est_mode_t;

/**
 * @brief BMS 估算遥测快照
 */
typedef struct {
    uint32_t sequence;       /**< 步计数（单调递增） */
    uint32_t status;         /**< 状态位组合（BM_BMS_EST_TEL_* 位域） */
    float    soc;            /**< 当前融合 SOC（0.0～1.0） */
    float    pack_voltage_v; /**< 电池包端电压（V） */
    float    pack_current_a; /**< 电池包电流（A），充电为正 */
    float    temp_c;         /**< 温度（°C） */
    bm_bms_est_mode_t est_mode; /**< 本帧估算模式 */
} bm_bms_est_telemetry_t;

/**
 * @brief 采样读取回调函数类型
 *
 * @param user          用户上下文指针
 * @param pack_current_a 输出：电池包电流（A）
 * @param pack_voltage_v 输出：电池包电压（V）
 * @param temp_c         输出：温度（°C）
 * @return 0 成功；非零 采样失败（遥测标记为 STALE）
 */
typedef int (*bm_bms_est_read_sample_fn)(void *user,
                                         float *pack_current_a,
                                         float *pack_voltage_v,
                                         float *temp_c);

/**
 * @brief 遥测发布回调函数类型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前帧遥测快照指针（只读）
 */
typedef void (*bm_bms_est_publish_telemetry_fn)(
    void *user,
    const bm_bms_est_telemetry_t *telemetry);

/**
 * @brief BMS 估算外部资源（回调绑定）
 */
typedef struct {
    bm_bms_est_read_sample_fn read_sample;             /**< 采样读取回调，可为 NULL */
    void                     *read_sample_user;         /**< read_sample 用户上下文 */
    bm_bms_est_publish_telemetry_fn publish_telemetry; /**< 遥测发布回调，可为 NULL */
    void                     *publish_telemetry_user;   /**< publish_telemetry 用户上下文 */
} bm_bms_estimation_resources_t;

/**
 * @brief BMS 估算静态配置
 */
typedef struct {
    bm_bms_est_mode_t             mode;              /**< 估算模式（FUSION 或 EKF） */
    bm_algo_coulomb_config_t      coulomb;           /**< 库仑计数配置 */
    bm_algo_battery_temp_config_t temp;              /**< 温度容量补偿配置 */
    bm_algo_soc_fusion_config_t   fusion;            /**< SOC 融合权重配置（FUSION 模式） */
    bm_algo_soc_ekf_config_t      soc_ekf;           /**< EKF 配置（EKF 模式） */
    const bm_algo_ocv_table_t    *ocv_table;         /**< OCV 查找表指针，可为 NULL */
    float                         ocv_slope_v_per_soc; /**< OCV 斜率（V/SOC），EKF 模式须 > 0 */
    float                         resting_current_a;  /**< 静息判定阈值（A） */
    float                         resting_time_s;     /**< 静息稳定所需时间（s），FUSION 模式有效 */
    float                         dt_s;               /**< 估算步长（s），须 > 0 */
    float                         soc_init;           /**< exec_init 时使用的初始 SOC */
} bm_bms_estimation_config_t;

/**
 * @brief BMS 估算运行时状态
 */
typedef struct {
    bm_algo_coulomb_state_t   coulomb;           /**< 库仑计数器内部状态 */
    bm_algo_soc_ekf_state_t   soc_ekf;           /**< SOC EKF 内部状态 */
    float                     soc_fused;         /**< 当前融合 SOC（0.0～1.0） */
    float                     resting_elapsed_s; /**< 累计静息时间（s） */
    uint32_t                  step_count;        /**< 已执行步数 */
    bm_bms_est_telemetry_t    telemetry;         /**< 最新遥测快照 */
} bm_bms_estimation_state_t;

/**
 * @brief BMS 估算完整实例（配置 + 资源 + 状态）
 */
typedef struct {
    bm_bms_estimation_config_t    config;    /**< 静态配置，初始化前填写 */
    bm_bms_estimation_resources_t resources; /**< 外部回调绑定 */
    bm_bms_estimation_state_t     state;     /**< 运行时状态，由 API 维护 */
} bm_bms_estimation_axis_t;

/**
 * @brief 校验 BMS 估算配置合法性
 *
 * @param config 指向待校验的配置结构体，不可为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数越界或指针为空
 */
int bm_bms_estimation_validate_config(const bm_bms_estimation_config_t *config);

/**
 * @brief 复位 BMS 估算实例状态
 *
 * 重置库仑计数器、EKF 状态及所有融合输出；以 soc_init 作为初始 SOC。
 *
 * @param axis     实例指针；为 NULL 时静默返回
 * @param soc_init 初始 SOC（0.0～1.0）
 */
void bm_bms_estimation_reset(bm_bms_estimation_axis_t *axis, float soc_init);

/**
 * @brief 执行一步 BMS SOC 估算
 *
 * 读取采样 → 温度容量补偿 → 库仑积分 → 静息检测 →
 * （EKF 模式）预测-更新 / （FUSION 模式）OCV 融合 → 发布遥测。
 * 采样读取失败时遥测标记 STALE，跳过本帧估算但递增步计数。
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_bms_estimation_step(bm_bms_estimation_axis_t *axis);

/**
 * @brief exec 封装：执行一步 SOC 估算（供调度框架调用）
 *
 * @param instance exec 实例指针，instance->state 须为 bm_bms_estimation_axis_t*
 */
void bm_bms_estimation_exec_step(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：初始化（校验配置并以 soc_init 复位）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_bms_estimation_exec_init(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：启动（当前无额外操作，保留扩展点）
 *
 * @param instance exec 实例指针
 * @return 始终返回 BM_OK
 */
int bm_bms_estimation_exec_start(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：安全停机（当前无额外操作）
 *
 * @param instance exec 实例指针
 */
void bm_bms_estimation_exec_safe_stop(const bm_exec_t *instance);

/** @brief bms_estimation 标准 exec ops 表，可直接赋给 bm_exec_t::ops */
extern const bm_exec_ops_t bm_bms_estimation_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_BMS_ESTIMATION_H */

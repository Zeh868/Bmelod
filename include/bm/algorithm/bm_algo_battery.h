/**
 * @file bm_algo_battery.h
 * @brief 电池算法：库仑计量、OCV-SOC 查表与 SOH 统计
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            SOH config 新增学习阈值与平滑系数字段；
 *                                                charge_ah 语义注释明确为原始 Ah（不含库仑效率）
 * 2026-08-01       1.1            Codex          补全算法 API Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_BATTERY_H
#define BM_ALGO_BATTERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 库仑计量 ---------- */
typedef struct {
    float nominal_capacity_ah;
    float coulomb_efficiency;
    float soc_min;
    float soc_max;
} bm_algo_coulomb_config_t;

typedef struct {
    float soc;
    float charge_ah; /**< 原始充电量累计（Ah，不含库仑效率），仅用于记录通过量；
                      *   SOC 积分另行乘以 coulomb_efficiency，两者语义不同，
                      *   请勿将 charge_ah 与 SOC 直接换算。*/
} bm_algo_coulomb_state_t;

/**
 * @brief 复位库仑计状态
 * @param state 算法状态
 * @param soc_init 初始荷电状态
 */
void bm_algo_coulomb_reset(bm_algo_coulomb_state_t *state, float soc_init);
/**
 * @brief 推进库仑计并计算本拍输出
 * @param state 算法状态
 * @param config 算法配置
 * @param current_a 电池电流，单位 A
 * @param dt_s 采样周期，单位 s
 * @return 更新后的荷电状态
 */
float bm_algo_coulomb_step(bm_algo_coulomb_state_t *state,
                           const bm_algo_coulomb_config_t *config,
                           float current_a,
                           float dt_s);

/* ---------- OCV-SOC 查表 ---------- */
typedef struct {
    const float *soc_table;
    const float *ocv_table;
    uint32_t point_count;
} bm_algo_ocv_table_t;

/**
 * @brief 根据开路电压表查找荷电状态
 * @param table 开路电压查找表
 * @param ocv_v 开路电压，单位 V
 * @return 查表插值得到的荷电状态
 */
float bm_algo_ocv_lookup_soc(const bm_algo_ocv_table_t *table, float ocv_v);
/**
 * @brief 根据荷电状态表查找开路电压
 * @param table 开路电压查找表
 * @param soc 荷电状态
 * @return 查表或补偿后的开路电压，单位 V
 */
float bm_algo_ocv_lookup_voltage(const bm_algo_ocv_table_t *table, float soc);

/* ---------- SOC 融合（库仑 + OCV 加权） ---------- */
typedef struct {
    float ocv_weight;  /**< 静置时 OCV 权重 [0,1] */
} bm_algo_soc_fusion_config_t;

/**
 * @brief 推进荷电状态融合并计算本拍输出
 * @param soc_coulomb 库仑计估算的荷电状态
 * @param soc_ocv 开路电压估算的荷电状态
 * @param config 算法配置
 * @return 更新后的荷电状态
 */
float bm_algo_soc_fusion_step(float soc_coulomb,
                              float soc_ocv,
                              const bm_algo_soc_fusion_config_t *config);

/* ---------- SOH（容量衰减统计） ---------- */
/**
 * @brief SOH 算法配置
 *
 * @note 学习策略：仅当单次放电量 discharged_ah 达到
 *       initial_capacity_ah * cycle_threshold_ratio 时才更新容量与循环计数，
 *       并对 learned_capacity_ah 做指数平滑：
 *       learned = (1 - smooth_alpha) * learned + smooth_alpha * discharged_ah。
 *
 * @note 合理默认值参考：cycle_threshold_ratio = 0.5f，smooth_alpha = 0.1f。
 */
typedef struct {
    float initial_capacity_ah;      /**< 出厂额定容量（Ah），用作 SOH 基准 */
    float cycle_threshold_ratio;    /**< 有效放电循环判定阈值（相对 initial_capacity_ah 的比例，
                                     *   建议 0.3~0.8，默认 0.5）；低于此值不计入循环也不更新容量 */
    float smooth_alpha;             /**< learned_capacity_ah 指数平滑系数（0 < alpha <= 1，
                                     *   建议 0.05~0.2，默认 0.1）；越小学习越保守 */
} bm_algo_soh_config_t;

typedef struct {
    float learned_capacity_ah;
    float cycle_count;
} bm_algo_soh_state_t;

/**
 * @brief 复位电池健康状态估计
 * @param state 算法状态
 * @param config 算法配置
 */
void bm_algo_soh_reset(bm_algo_soh_state_t *state,
                       const bm_algo_soh_config_t *config);
/**
 * @brief 更新电池健康状态估计
 * @param state 算法状态
 * @param config 算法配置
 * @param discharged_ah 累计放电容量，单位 Ah
 * @return 更新后的健康状态比例
 */
float bm_algo_soh_update(bm_algo_soh_state_t *state,
                         const bm_algo_soh_config_t *config,
                         float discharged_ah);

/* ---------- 温度补偿（容量/SOC 一阶修正） ---------- */
typedef struct {
    float ref_temp_c;
    float capacity_coeff_per_c;  /**< 有效容量温度系数（1/°C） */
    float ocv_shift_v_per_c;     /**< OCV 电压温度漂移（V/°C） */
} bm_algo_battery_temp_config_t;

/**
 * @brief 按温度修正电池可用容量
 * @param nominal_capacity_ah 参考温度下的标称容量，单位 Ah
 * @param temp_c 当前电池温度，单位 °C
 * @param config 电池温度补偿配置
 * @return 温度补偿后的可用容量，单位 Ah；参数无效时返回原标称容量
 */
float bm_algo_battery_temp_capacity_ah(float nominal_capacity_ah,
                                       float temp_c,
                                       const bm_algo_battery_temp_config_t *config);

/**
 * @brief 按温度补偿电池开路电压
 * @param ocv_v 待补偿的开路电压，单位 V
 * @param temp_c 当前电池温度，单位 °C
 * @param config 电池温度补偿配置
 * @return 补偿到参考温度的开路电压，单位 V；参数无效时返回原开路电压
 */
float bm_algo_battery_temp_compensate_ocv(float ocv_v,
                                          float temp_c,
                                          const bm_algo_battery_temp_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_BATTERY_H */

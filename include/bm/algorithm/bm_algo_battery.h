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

void bm_algo_coulomb_reset(bm_algo_coulomb_state_t *state, float soc_init);
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

float bm_algo_ocv_lookup_soc(const bm_algo_ocv_table_t *table, float ocv_v);
float bm_algo_ocv_lookup_voltage(const bm_algo_ocv_table_t *table, float soc);

/* ---------- SOC 融合（库仑 + OCV 加权） ---------- */
typedef struct {
    float ocv_weight;  /**< 静置时 OCV 权重 [0,1] */
} bm_algo_soc_fusion_config_t;

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

void bm_algo_soh_reset(bm_algo_soh_state_t *state,
                       const bm_algo_soh_config_t *config);
float bm_algo_soh_update(bm_algo_soh_state_t *state,
                         const bm_algo_soh_config_t *config,
                         float discharged_ah);

/* ---------- 温度补偿（容量/SOC 一阶修正） ---------- */
typedef struct {
    float ref_temp_c;
    float capacity_coeff_per_c;  /**< 有效容量温度系数（1/°C） */
    float ocv_shift_v_per_c;     /**< OCV 电压温度漂移（V/°C） */
} bm_algo_battery_temp_config_t;

/** 按温度修正标称容量（Ah） */
float bm_algo_battery_temp_capacity_ah(float nominal_capacity_ah,
                                       float temp_c,
                                       const bm_algo_battery_temp_config_t *config);

/** 按温度修正 OCV 查表输入电压（V） */
float bm_algo_battery_temp_compensate_ocv(float ocv_v,
                                          float temp_c,
                                          const bm_algo_battery_temp_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_BATTERY_H */

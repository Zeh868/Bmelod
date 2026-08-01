/**
 * @file bmp_algo_bms.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief K2 · 闭源 · 需 bm_mp 的 BMS SOC 融合：库仑计量 + OCV 加权
 * @maturity E1
 * @author Bmelod contributors
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 * Date       Version Author Description
 * 2026-08-01 1.0     Codex  补齐规范化文件头元数据
 */
#ifndef BMP_ALGO_BMS_H
#define BMP_ALGO_BMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BMS 融合配置
 *
 * OCV 表三个字段全部提供（非 NULL 指针且 point_count >= 2）时启用自定义
 * 电池化学曲线；否则回退内部默认 3 点表 {3.0,3.6,4.2}V ↔ {0,0.5,1.0}，
 * 保证只填容量/权重的旧调用行为不变。soc_table 与 ocv_table 须等长、
 * 生命周期覆盖 fusion_step 调用期。
 */
typedef struct {
    float nominal_capacity_ah;      /**< 标称容量（Ah，必须 > 0） */
    float ocv_weight;               /**< OCV 融合权重 */
    const float *ocv_soc_table;     /**< SOC 断点数组，NULL=用默认表 */
    const float *ocv_voltage_table; /**< 对应 OCV 电压断点数组，NULL=用默认表 */
    uint32_t     ocv_point_count;   /**< 断点数（>=2 生效），0=用默认表 */
} bmp_bms_config_t;

typedef struct {
    float soc_coulomb;
    float charge_ah;
    uint8_t initialized;
    uint8_t reserved[3];
} bmp_bms_state_t;

/**
 * @brief 初始化 BMS SOC 融合状态
 * @param state BMS SOC 融合状态
 * @param config BMS SOC 融合配置
 * @param soc_init 初始荷电状态
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bmp_bms_fusion_init(bmp_bms_state_t *state,
                        const bmp_bms_config_t *config,
                        float soc_init);

/**
 * @brief 执行一步 BMS SOC 融合
 * @param state BMS SOC 融合状态
 * @param config BMS SOC 融合配置
 * @param current_a 电池电流，单位 A
 * @param voltage_v 电池端电压，单位 V
 * @param dt_s 本次更新的时间间隔，单位 s
 * @param soc_out 输出的融合荷电状态
 * @return BM_OK 成功；BM_ERR_INVALID 参数或状态无效
 */
int bmp_bms_fusion_step(bmp_bms_state_t *state,
                        const bmp_bms_config_t *config,
                        float current_a,
                        float voltage_v,
                        float dt_s,
                        float *soc_out);

#ifdef __cplusplus
}
#endif

#endif /* BMP_ALGO_BMS_H */

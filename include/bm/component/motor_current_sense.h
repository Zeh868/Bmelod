/**
 * @file motor_current_sense.h
 * @brief 电机分流电阻电流采样重构（2/3 分流）
 *
 * 支持 2-shunt 与 3-shunt 两种拓扑；ADC 路径读取注入通道原始值，
 * sim_fb 路径用于仿真/HIL 直接注入浮点电流。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            PWM 扇区采样窗口判定
 * 2026-06-23       0.3            zeh            validate_config 字段校验；公共 API Doxygen；SPDX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_MOTOR_CURRENT_SENSE_H
#define BM_MOTOR_CURRENT_SENSE_H

#include "bm/algorithm/bm_algo_motor.h"
#include "hal/bm_hal_adc.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BM_MOTOR_CS_2SHUNT = 0,
    BM_MOTOR_CS_3SHUNT
} bm_motor_current_sense_topology_t;

typedef struct {
    float *ia_a;
    float *ib_a;
    float *ic_a;
} bm_motor_current_sense_sim_fb_t;

typedef struct {
    bm_hal_adc_t *adc;
    uint32_t      rank_ia;
    uint32_t      rank_ib;
    uint32_t      rank_ic;
    float         adc_scale;
    bm_motor_current_sense_sim_fb_t sim_fb;
} bm_motor_current_sense_resources_t;

typedef struct {
    bm_motor_current_sense_topology_t topology;
    float                             offset_a;
    uint32_t                          pwm_sector;
    float                             adc_phase_deg;
    float                             sample_window_deg;
} bm_motor_current_sense_config_t;

typedef struct {
    bm_algo_abc_t      abc;
    bm_algo_alphabeta_t alphabeta;
    int                valid;
    int                sample_valid;
} bm_motor_current_sense_state_t;

typedef struct {
    bm_motor_current_sense_config_t    config;
    bm_motor_current_sense_resources_t resources;
    bm_motor_current_sense_state_t     state;
} bm_motor_current_sense_axis_t;

/**
 * @brief 校验电流采样配置合法性
 *
 * 检查 topology 枚举值、adc_phase_deg（[0, 360)）及
 * sample_window_deg（若非零则须 > 0 且 < 180）。
 *
 * @param config 待校验配置指针，不得为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数非法
 */
int  bm_motor_current_sense_validate_config(
    const bm_motor_current_sense_config_t *config);

/**
 * @brief 初始化电流采样轴
 *
 * 校验配置，检查 ADC/sim_fb 资源一致性，并执行 reset。
 *
 * @param axis 轴实例指针，不得为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 参数或资源非法
 */
int  bm_motor_current_sense_init(bm_motor_current_sense_axis_t *axis);

/**
 * @brief 复位电流采样状态（清零 abc/alphabeta/valid 标志）
 *
 * @param axis 轴实例指针；NULL 时直接返回
 */
void bm_motor_current_sense_reset(bm_motor_current_sense_axis_t *axis);

/**
 * @brief 执行一次电流采样步骤
 *
 * 依次进行采样窗口判断（若 sample_window_deg > 0）、ADC 或 sim_fb
 * 读取、Clarke 变换，结果写入 axis->state。
 *
 * @param axis 轴实例指针，不得为 NULL
 * @return BM_OK 成功；BM_ERR_INVALID 窗口无效或 ADC 读取失败
 */
int  bm_motor_current_sense_step(bm_motor_current_sense_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTOR_CURRENT_SENSE_H */

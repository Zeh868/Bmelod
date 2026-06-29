/**
 * @file motor_foc_sensored.h
 * @brief 有感 FOC 伺服轴领域组件（电流环 + 速度环 + 编码器）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.5
 * @date 2026-06-24
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-22       0.2            zeh            B3 诊断：遥测加 ia_raw/ib_raw 原始 ADC 字段
 * 2026-06-23       0.3            zeh            MTPA/弱磁支持：config 加 enable_mtpa/enable_fw
 *                                                及电机参数字段，state 加 last_vd/vq_pu
 * 2026-06-23       0.4            zeh            encoder 丢样容忍：opt-in encoder_timeout_s
 *                                                （默认 0=旧行为），speed_step 超时才 latch
 * 2026-06-24       0.5            zeh            config 加 opt-in speed_feedback_sign
 *                                                （<0 翻 speed_meas，修镜像轴速度环正反馈跑飞）
 *
 * 单实例包含双 HRT 槽语义：快环电流、慢环速度。命令/遥测由应用经 snapshot 注入。
 * HAL 句柄经 resources 注入，不包含板级初始化。
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_MOTOR_FOC_SENSORED_H
#define BM_MOTOR_FOC_SENSORED_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_motion.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/hybrid/bm_exec.h"
#include "hal/bm_hal_adc.h"
#include "hal/bm_hal_encoder.h"
#include "hal/bm_hal_pwm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_MOTOR_FOC_CMD_ENABLED  (1u << 0u)
#define BM_MOTOR_FOC_CMD_FAULT    (1u << 1u)

#define BM_MOTOR_FOC_TEL_VALID    (1u << 0u)
#define BM_MOTOR_FOC_TEL_SAT      (1u << 1u)
#define BM_MOTOR_FOC_TEL_FAULT    (1u << 2u)

/** SRT → HRT 命令（由应用写入 state.cmd） */
typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    speed_setpoint_rad_s;
    float    id_ref_a;
} bm_motor_foc_cmd_t;

/**
 * @brief HRT → SRT 遥测（由组件写入 state.telemetry）。
 *
 * B3 诊断字段（由 current_step 在读取 ADC 后回填）：
 *   ia_raw / ib_raw：Clarke 变换前的原始 ADC 计数（uint16，0~65535，中心~32768）。
 *   用于量化 M0（SENSOR_VP/VN 噪声脚）vs M1（普通 GPIO）的噪声底差异。
 *   应用层通过 publish_telemetry 回调把这两个字段录入黑匣子帧。
 */
typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    id_meas_a;
    float    iq_meas_a;
    float    speed_rad_s;
    float    theta_elec_rad;
    float    iq_ref_a;
    uint16_t ia_raw;  /**< B3 诊断：ia 原始 ADC 计数（Clarke 变换前，ADC 读值）。 */
    uint16_t ib_raw;  /**< B3 诊断：ib 原始 ADC 计数（Clarke 变换前，ADC 读值）。 */
} bm_motor_foc_telemetry_t;

/**
 * @brief native_sim 可选：注入 plant 电流/角度，绕开 ADC 极性偏差（实机置 NULL）
 */
typedef struct {
    float *id_a;
    float *iq_a;
    float *theta_elec_rad;
} bm_motor_foc_sim_feedback_t;

/**
 * @brief 电压施加后回调（Demo plant：RL + 机械模型 + 编码器计数）
 */
typedef void (*bm_motor_foc_on_voltage_fn)(void *user,
                                           float vd_pu,
                                           float vq_pu,
                                           float theta_elec_rad);

typedef int (*bm_motor_foc_read_command_fn)(void *user,
                                            bm_motor_foc_cmd_t *command);

typedef void (*bm_motor_foc_publish_telemetry_fn)(
    void *user,
    const bm_motor_foc_telemetry_t *telemetry);

typedef struct {
    const bm_hal_adc_t *adc;
    const bm_hal_pwm_t *pwm;
    const bm_hal_encoder_t *encoder;
    uint32_t adc_rank_ia;
    uint32_t adc_rank_ib;
    uint16_t pwm_max;
    float    current_adc_scale;
    bm_motor_foc_sim_feedback_t sim_fb;
    bm_motor_foc_on_voltage_fn on_voltage;
    void    *on_voltage_user;
    bm_motor_foc_read_command_fn read_command;
    void    *read_command_user;
    bm_motor_foc_publish_telemetry_fn publish_telemetry;
    void    *publish_telemetry_user;
} bm_motor_foc_sensored_resources_t;

typedef struct {
    float    pole_pairs;
    float    encoder_direction;
    float    electrical_offset_rad;
    float    vbus_v;
    float    phase_r_ohm;
    float    v_max_pu;
    float    current_dt_s;
    float    speed_dt_s;
    float    iq_max_a;
    /**
     * @brief encoder 读丢样容忍超时（s）。0=旧行为（读失败即 latch_fault）。
     *
     * >0 时 speed_step 对编码器读失败短时容忍：保持上次有效机械速度继续算速度环，
     * 同时累计连续丢样时长，超过本阈值才 latch_fault 进安全态。读成功即清零累计。
     * 用于滤除 I2C 偶发单拍读失败（总线竞争），避免一次毛刺拖整车进安全态。
     */
    float    encoder_timeout_s;
    /**
     * @brief 速度环反馈符号修正（0 或 +1=不翻；<0=翻转）。
     *
     * encoder 测得的机械速度符号纯由原始计数增减方向决定（bm_algo_encoder_update
     * 不经 encoder_direction）。镜像安装的轴：+iq 产生的转矩使转子正转，但 encoder
     * 计数反向递减 → 测速符号与转矩约定相反 → 速度环负反馈变正反馈 → 跑飞。
     * 本字段 <0 时 speed_step 对 speed_meas 取反，修正该轴反馈极性。
     * 默认 0（=不翻，向后兼容）；按轴标定（如双轮一轴 +1、镜像轴 -1）。
     */
    float    speed_feedback_sign;
    /**
     * @brief 使能 MTPA（最大转矩电流比）电流分配。
     *
     * 非零时由 bm_algo_mtpa_id_ref() 按 iq_ref 自动计算最优 id_ref，
     * 充分利用凸极效应降低铜损；零时 id_ref 沿用命令层传入值（默认行为不变）。
     * 启用时须同时配置 ld_h / lq_h / psi_f_wb。
     */
    int      enable_mtpa;
    /**
     * @brief 使能弱磁（Field Weakening）电压饱和时 id 调节。
     *
     * 非零时当电压矢量接近 v_max_pu 时由 bm_algo_fw_id_adjust() 下调 id_ref，
     * 扩展高速转速范围；零时不调整（默认行为不变）。
     */
    int      enable_fw;
    /** @brief d 轴电感（H），MTPA 计算所需；enable_mtpa=0 时可置 0。 */
    float    ld_h;
    /** @brief q 轴电感（H），MTPA 计算所需；enable_mtpa=0 时可置 0。 */
    float    lq_h;
    /** @brief 永磁体磁链（Wb），MTPA 计算所需；enable_mtpa=0 时可置 0。 */
    float    psi_f_wb;
    bm_algo_pi_config_t pi_d;
    bm_algo_pi_config_t pi_q;
    bm_algo_pi_config_t pi_speed;
    bm_algo_ramp_config_t speed_ramp;
    bm_algo_encoder_config_t encoder;
} bm_motor_foc_sensored_config_t;

/** 快环状态（仅电流槽写） */
typedef struct {
    bm_algo_pi_state_t pi_d;
    bm_algo_pi_state_t pi_q;
    uint32_t           loop_count;
    /** @brief 上一拍 d 轴输出电压（pu），弱磁算法输入，由 current_step 更新。 */
    float              last_vd_pu;
    /** @brief 上一拍 q 轴输出电压（pu），弱磁算法输入，由 current_step 更新。 */
    float              last_vq_pu;
} bm_motor_foc_current_state_t;

/** 慢环状态（仅速度槽写） */
typedef struct {
    bm_algo_pi_state_t      pi_speed;
    bm_algo_ramp_state_t    speed_ramp;
    bm_algo_encoder_state_t encoder;
    float                   iq_ref_a;
    uint32_t                last_cmd_sequence;
    /** @brief encoder 连续丢样累计时长（s），读成功清零。encoder_timeout_s>0 时用。 */
    float                   encoder_lost_time_s;
    /** @brief 上次有效机械速度（rad/s），丢样容忍窗内保持喂速度环。 */
    float                   last_velocity_rad_s;
} bm_motor_foc_speed_state_t;

typedef struct {
    bm_motor_foc_cmd_t        cmd;
    bm_motor_foc_telemetry_t  telemetry;
    bm_motor_foc_current_state_t current;
    bm_motor_foc_speed_state_t   speed;
    int                       fault_latched;
    uint32_t                  fault_count;
} bm_motor_foc_sensored_state_t;

typedef struct {
    bm_motor_foc_sensored_config_t     config;
    bm_motor_foc_sensored_resources_t  resources;
    bm_motor_foc_sensored_state_t        state;
} bm_motor_foc_sensored_axis_t;

int bm_motor_foc_sensored_validate_config(
    const bm_motor_foc_sensored_config_t *config);

void bm_motor_foc_sensored_reset(bm_motor_foc_sensored_axis_t *axis);

void bm_motor_foc_sensored_apply_command(bm_motor_foc_sensored_axis_t *axis,
                                        const bm_motor_foc_cmd_t *cmd);

void bm_motor_foc_sensored_current_step(bm_motor_foc_sensored_axis_t *axis);

void bm_motor_foc_sensored_speed_step(bm_motor_foc_sensored_axis_t *axis);

/** bm_exec 槽回调：instance->state 须指向 bm_motor_foc_sensored_axis_t */
void bm_motor_foc_sensored_exec_current(const bm_exec_t *instance);

void bm_motor_foc_sensored_exec_speed(const bm_exec_t *instance);

int bm_motor_foc_sensored_exec_init(const bm_exec_t *instance);

int bm_motor_foc_sensored_exec_start(const bm_exec_t *instance);

void bm_motor_foc_sensored_exec_safe_stop(const bm_exec_t *instance);

extern const bm_exec_ops_t bm_motor_foc_sensored_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTOR_FOC_SENSORED_H */

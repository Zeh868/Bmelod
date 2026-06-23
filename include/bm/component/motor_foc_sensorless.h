/**
 * @file motor_foc_sensorless.h
 * @brief 无感 FOC 领域组件（启动状态机 + 磁链观测 + MTPA/弱磁 + 电流环）
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
 * 2026-06-17       0.2            zeh            增加启动状态机
 * 2026-06-23       0.3            zeh            补 SPDX 与函数级 Doxygen
 *
 * 电流环使用磁链观测器角度；命令 iq_ref_a 为 q 轴电流参考（A）。
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_MOTOR_FOC_SENSORLESS_H
#define BM_MOTOR_FOC_SENSORLESS_H

#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/hybrid/bm_exec.h"
#include "hal/bm_hal_adc.h"
#include "hal/bm_hal_pwm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 命令状态位：使能电机 */
#define BM_MOTOR_SL_CMD_ENABLED  (1u << 0u)
/** @brief 命令状态位：外部故障锁定 */
#define BM_MOTOR_SL_CMD_FAULT    (1u << 1u)

/** @brief 遥测状态位：数据有效 */
#define BM_MOTOR_SL_TEL_VALID    (1u << 0u)
/** @brief 遥测状态位：电压矢量已饱和 */
#define BM_MOTOR_SL_TEL_SAT      (1u << 1u)
/** @brief 遥测状态位：当前帧处于故障态 */
#define BM_MOTOR_SL_TEL_FAULT    (1u << 2u)

/**
 * @brief 无感 FOC 启动状态机相位枚举
 */
typedef enum {
    BM_MOTOR_SL_PHASE_IDLE = 0,  /**< 空闲：PWM 关断 */
    BM_MOTOR_SL_PHASE_ALIGN,     /**< 预对齐：固定角度注入 id */
    BM_MOTOR_SL_PHASE_OPEN_LOOP, /**< 开环升速：线性 omega 斜坡 */
    BM_MOTOR_SL_PHASE_OBSERVER,  /**< 闭环：磁链观测器跟踪角度 */
    BM_MOTOR_SL_PHASE_FAULT      /**< 故障：PWM 安全状态 */
} bm_motor_sl_phase_t;

/**
 * @brief 无感 FOC 控制命令
 */
typedef struct {
    uint32_t sequence;  /**< 命令序列号 */
    uint32_t status;    /**< 命令状态位（BM_MOTOR_SL_CMD_* 位域） */
    float    iq_ref_a;  /**< q 轴电流参考（A） */
} bm_motor_sl_cmd_t;

/**
 * @brief 无感 FOC 遥测快照
 */
typedef struct {
    uint32_t sequence;       /**< 步计数（单调递增） */
    uint32_t status;         /**< 遥测状态位（BM_MOTOR_SL_TEL_* 位域） */
    float    id_meas_a;      /**< d 轴实测电流（A） */
    float    iq_meas_a;      /**< q 轴实测电流（A） */
    float    theta_elec_rad; /**< 电气角（rad） */
    float    omega_rad_s;    /**< 电气角速度（rad/s） */
    float    iq_ref_a;       /**< 本帧实际 q 轴电流参考（A，已限幅） */
    bm_motor_sl_phase_t phase; /**< 当前启动相位 */
} bm_motor_sl_telemetry_t;

/**
 * @brief 仿真反馈注入结构体（硬件在环 / 纯软件仿真）
 *
 * 三个指针均非 NULL 时组件旁路 ADC 读取，直接使用仿真值。
 */
typedef struct {
    float *id_a;           /**< 仿真 d 轴电流（A） */
    float *iq_a;           /**< 仿真 q 轴电流（A） */
    float *theta_elec_rad; /**< 仿真电气角（rad） */
} bm_motor_sl_sim_feedback_t;

/**
 * @brief 电压矢量输出通知回调（可选，用于仿真/记录）
 *
 * @param user           用户上下文指针
 * @param vd_pu          d 轴电压标幺值
 * @param vq_pu          q 轴电压标幺值
 * @param theta_elec_rad 当前电气角（rad）
 */
typedef void (*bm_motor_sl_on_voltage_fn)(void *user,
                                          float vd_pu,
                                          float vq_pu,
                                          float theta_elec_rad);

/**
 * @brief 命令读取回调函数类型
 *
 * @param user    用户上下文指针
 * @param command 输出：最新命令
 * @return 0 成功；非零 无新命令（保持旧命令）
 */
typedef int (*bm_motor_sl_read_command_fn)(void *user,
                                           bm_motor_sl_cmd_t *command);

/**
 * @brief 遥测发布回调函数类型
 *
 * @param user      用户上下文指针
 * @param telemetry 当前帧遥测快照指针（只读）
 */
typedef void (*bm_motor_sl_publish_telemetry_fn)(
    void *user,
    const bm_motor_sl_telemetry_t *telemetry);

/**
 * @brief 无感 FOC 外部资源（HAL 句柄 + 回调绑定）
 */
typedef struct {
    const bm_hal_adc_t *adc;            /**< ADC 句柄，use_sim=0 时必须非 NULL */
    const bm_hal_pwm_t *pwm;            /**< PWM 句柄，可为 NULL（仅仿真） */
    uint32_t adc_rank_ia;               /**< ADC 注入序列：A 相电流 rank */
    uint32_t adc_rank_ib;               /**< ADC 注入序列：B 相电流 rank */
    uint16_t pwm_max;                   /**< PWM 计数器最大值（占空比比例因子） */
    float    current_adc_scale;         /**< 电流 ADC 满码对应电流（A），须 > 0 */
    bm_motor_sl_sim_feedback_t sim_fb;  /**< 仿真反馈注入，全 NULL 时使用真实 ADC */
    bm_motor_sl_on_voltage_fn on_voltage;     /**< 电压通知回调，可为 NULL */
    void    *on_voltage_user;                 /**< on_voltage 用户上下文 */
    bm_motor_sl_read_command_fn read_command; /**< 命令读取回调，可为 NULL */
    void    *read_command_user;               /**< read_command 用户上下文 */
    bm_motor_sl_publish_telemetry_fn publish_telemetry; /**< 遥测发布回调，可为 NULL */
    void    *publish_telemetry_user;          /**< publish_telemetry 用户上下文 */
} bm_motor_foc_sensorless_resources_t;

/**
 * @brief 无感 FOC 静态配置
 */
typedef struct {
    float    pole_pairs;              /**< 极对数 */
    float    vbus_v;                  /**< 母线电压（V），须 > 0 */
    float    phase_r_ohm;             /**< 相电阻（Ω），须 > 0 */
    float    ld_h;                    /**< d 轴电感（H），enable_mtpa=1 时须 > 0 */
    float    lq_h;                    /**< q 轴电感（H），enable_mtpa=1 时须 > 0 */
    float    psi_f_wb;                /**< 永磁磁链（Wb），MTPA 计算用 */
    float    v_max_pu;                /**< 电压矢量限幅（标幺值） */
    float    current_dt_s;            /**< 电流环步长（s），须 > 0 */
    float    iq_max_a;                /**< q 轴电流最大值（A），须 > 0 */
    int      enable_mtpa;             /**< 非零：使能最大转矩电流比（MTPA） */
    int      enable_fw;               /**< 非零：使能弱磁控制 */
    float    align_time_s;            /**< 预对齐持续时间（s），≤0 时内部取 0.2 */
    float    align_id_a;              /**< 预对齐注入 d 轴电流（A），≤0 时内部取 0.3 */
    float    open_loop_omega_start;   /**< 开环初始电气角速度（rad/s），≤0 时取 20 */
    float    open_loop_omega_end;     /**< 开环目标电气角速度（rad/s），≤0 时取 200 */
    float    open_loop_ramp_s;        /**< 开环升速斜坡时间（s），≤0 时取 0.5 */
    float    observer_lock_omega_rad_s; /**< 观测器锁定判定最小角速度（rad/s） */
    float    observer_lock_time_s;    /**< 角速度低于锁定阈值持续此时间后触发故障（s） */
    bm_algo_pi_config_t pi_d;         /**< d 轴电流 PI 配置 */
    bm_algo_pi_config_t pi_q;         /**< q 轴电流 PI 配置 */
    bm_algo_flux_observer_config_t observer; /**< 磁链观测器配置 */
} bm_motor_foc_sensorless_config_t;

/**
 * @brief 无感 FOC 运行时状态
 */
typedef struct {
    bm_motor_sl_cmd_t cmd;              /**< 最新控制命令 */
    bm_motor_sl_telemetry_t telemetry;  /**< 最新遥测快照 */
    bm_motor_sl_phase_t phase;          /**< 当前启动相位 */
    float phase_timer_s;                /**< 当前相位内计时（s） */
    float open_loop_theta;              /**< 开环电气角（rad） */
    float open_loop_omega;              /**< 开环电气角速度（rad/s） */
    float lock_loss_timer_s;            /**< 低速计时（角速度低于锁定阈值的累计时间，s） */
    bm_algo_pi_state_t pi_d;            /**< d 轴 PI 积分器状态 */
    bm_algo_pi_state_t pi_q;            /**< q 轴 PI 积分器状态 */
    bm_algo_flux_observer_state_t observer; /**< 磁链观测器状态 */
    float last_vd_pu;                   /**< 上一帧 d 轴电压标幺值（用于观测器反馈） */
    float last_vq_pu;                   /**< 上一帧 q 轴电压标幺值（用于观测器反馈） */
    uint32_t loop_count;                /**< 电流环执行次数 */
    int fault_latched;                  /**< 非零：故障已锁存，须外部复位才能清除 */
    uint32_t fault_count;               /**< 累计故障锁存次数 */
} bm_motor_foc_sensorless_state_t;

/**
 * @brief 无感 FOC 完整实例（配置 + 资源 + 状态）
 */
typedef struct {
    bm_motor_foc_sensorless_config_t    config;    /**< 静态配置，初始化前填写 */
    bm_motor_foc_sensorless_resources_t resources; /**< 外部资源绑定 */
    bm_motor_foc_sensorless_state_t     state;     /**< 运行时状态，由 API 维护 */
} bm_motor_foc_sensorless_axis_t;

/**
 * @brief 校验无感 FOC 配置合法性
 *
 * @param config 指向待校验的配置结构体，不可为 NULL
 * @return BM_OK 合法；BM_ERR_INVALID 参数越界或指针为空
 */
int bm_motor_foc_sensorless_validate_config(
    const bm_motor_foc_sensorless_config_t *config);

/**
 * @brief 复位无感 FOC 实例状态（相位回 IDLE，PI/观测器清零）
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_motor_foc_sensorless_reset(bm_motor_foc_sensorless_axis_t *axis);

/**
 * @brief 应用外部控制命令（使能/故障/iq_ref）
 *
 * 若命令携带 FAULT 位则立即锁存故障并将 PWM 置安全状态。
 * 若命令携带 ENABLED 且当前处于 IDLE 则启动预对齐序列。
 *
 * @param axis 实例指针
 * @param cmd  待应用的命令，不可为 NULL
 */
void bm_motor_foc_sensorless_apply_command(bm_motor_foc_sensorless_axis_t *axis,
                                           const bm_motor_sl_cmd_t *cmd);

/**
 * @brief 执行一步无感 FOC 电流环
 *
 * 按当前相位（ALIGN / OPEN_LOOP / OBSERVER）计算 d/q 轴电流参考，
 * 读取反馈（ADC 或仿真），运行 PI 控制器，生成 SVPWM 占空比并写入 PWM。
 * 同步更新 state.telemetry。
 *
 * @param axis 实例指针；为 NULL 时静默返回
 */
void bm_motor_foc_sensorless_current_step(bm_motor_foc_sensorless_axis_t *axis);

/**
 * @brief exec 封装：同步命令后执行一步电流环并发布遥测（供调度框架调用）
 *
 * @param instance exec 实例指针，instance->state 须为 bm_motor_foc_sensorless_axis_t*
 */
void bm_motor_foc_sensorless_exec_current(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：初始化（校验配置并复位状态）
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法或指针为空
 */
int bm_motor_foc_sensorless_exec_init(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：启动（使能 PWM 输出）
 *
 * 若 resources.pwm 非 NULL 则调用 bm_hal_pwm_enable_outputs() 使能桥臂。
 *
 * @param instance exec 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 指针为空
 */
int bm_motor_foc_sensorless_exec_start(const bm_exec_t *instance);

/**
 * @brief exec 生命周期：安全停机（触发 PWM 安全状态）
 *
 * 调用 bm_hal_pwm_request_safe_state() 将桥臂置于安全状态（硬件制动/关断）。
 *
 * @param instance exec 实例指针
 */
void bm_motor_foc_sensorless_exec_safe_stop(const bm_exec_t *instance);

/** @brief motor_foc_sensorless 标准 exec ops 表，可直接赋给 bm_exec_t::ops */
extern const bm_exec_ops_t bm_motor_foc_sensorless_exec_ops;

#ifdef __cplusplus
}
#endif

#endif /* BM_MOTOR_FOC_SENSORLESS_H */

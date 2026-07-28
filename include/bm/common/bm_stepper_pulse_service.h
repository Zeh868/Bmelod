/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_stepper_pulse_service.h
 * @brief STEP/DIR 脉冲服务的零组件依赖契约
 *
 * 供步进脉冲组件及其平台适配器共享的配置、静态实例和操作接口。
 * 本头文件只依赖 common 层，适配器不得通过组件头访问步进脉冲实现。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            从组件头下沉步进脉冲服务契约
 */
#ifndef BM_STEPPER_PULSE_SERVICE_H
#define BM_STEPPER_PULSE_SERVICE_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STEP/DIR 脉冲服务配置。
 *
 * ISR 翻转方式建议 `max_step_rate_hz` 不超过 10kHz；`dir_hold_us`、
 * `min_high_us` 与 `min_low_us` 为 0 时不施加对应的额外时序约束。
 */
typedef struct {
    uint32_t max_step_rate_hz; /**< 脉冲频率上限（steps/s）。 */
    uint32_t dir_setup_us;     /**< DIR 建立时间（μs）。 */
    uint32_t dir_hold_us;      /**< DIR 切换前 STEP 低电平保持时间（μs）。 */
    uint32_t min_high_us;      /**< STEP 高电平最小时间（μs）。 */
    uint32_t min_low_us;       /**< STEP 低电平最小时间（μs）。 */
} bm_stepper_pulse_config_t;

/** @brief STEP/DIR 脉冲服务的平台资源回调；user 统一透传。 */
typedef struct {
    int  (*step_high)(void *user);
    int  (*step_low)(void *user);
    int  (*dir_set)(void *user, int level);
    int  (*en_set)(void *user, int level);
    /**
     * @brief 请求下一次定时器到期不晚于指定间隔。
     * @param user 平台私有上下文。
     * @param interval_us 到期时间上限；0 表示取消定时器。
     * @return BM_OK 成功；否则为平台错误码。
     */
    int  (*arm_timer)(void *user, uint32_t interval_us);
    void *user;
} bm_stepper_pulse_resources_t;

/** @brief STEP/DIR 脉冲服务运行状态。 */
typedef struct {
    int32_t  position;
    float    velocity_sps;
    int      dir;
    uint8_t  step_level;
    uint8_t  running;
    uint8_t  dir_wait_pending;
    uint8_t  dir_hold_pending;
    uint8_t  fault;
} bm_stepper_pulse_state_t;

/** @brief STEP/DIR 脉冲服务静态轴实例；由调用方静态分配。 */
typedef struct {
    bm_stepper_pulse_config_t    config;
    bm_stepper_pulse_resources_t resources;
    bm_stepper_pulse_state_t     state;
} bm_stepper_pulse_axis_t;

/**
 * @brief 校验脉冲服务配置。
 * @param config 待校验配置。
 * @return BM_OK 合法；BM_ERR_INVALID 参数非法或时序约束冲突。
 */
int bm_stepper_pulse_validate_config(const bm_stepper_pulse_config_t *config);

/**
 * @brief 初始化静态轴实例。
 * @param axis 静态轴实例。
 * @return BM_OK 成功；BM_ERR_INVALID 指针、配置或必需回调非法。
 */
int bm_stepper_pulse_init(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 复位轴实例并取消定时器，位置与速度清零。
 * @param axis 静态轴实例；NULL 时静默返回。
 */
void bm_stepper_pulse_reset(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 清除故障锁存，不改变位置和速度。
 * @param axis 静态轴实例；NULL 时静默返回。
 */
void bm_stepper_pulse_clear_fault(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 设置目标速度，符号表示方向，幅值受配置上限钳制。
 * @param axis 静态轴实例；NULL 时静默返回。
 * @param velocity_sps 目标速度（steps/s）；0 等价于停止。
 */
void bm_stepper_pulse_set_velocity(bm_stepper_pulse_axis_t *axis,
                                   float velocity_sps);

/**
 * @brief 停止脉冲输出并取消定时器，位置保持不变。
 * @param axis 静态轴实例；NULL 时静默返回。
 */
void bm_stepper_pulse_stop(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 设置 EN 电平。
 * @param axis 静态轴实例。
 * @param enable 非 0 使能；0 禁用。
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；BM_ERR_NOT_SUPPORTED 无 EN 回调；
 *         其他 BM_ERR_* 为 GPIO 或已锁存故障。
 */
int bm_stepper_pulse_set_enable(bm_stepper_pulse_axis_t *axis, int enable);

/**
 * @brief 获取步进位置。
 * @param axis 静态轴实例；NULL 时返回 0。
 * @return 带符号的步进位置。
 */
int32_t bm_stepper_pulse_position(const bm_stepper_pulse_axis_t *axis);

/**
 * @brief 定时器到期入口，由平台 ISR 上下文调用。
 * @param axis 静态轴实例；NULL 时静默返回。
 */
void bm_stepper_pulse_on_timer(bm_stepper_pulse_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_STEPPER_PULSE_SERVICE_H */

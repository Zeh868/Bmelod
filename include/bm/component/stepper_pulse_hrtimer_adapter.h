/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse_hrtimer_adapter.h
 * @brief STEP/DIR 脉冲服务与高精度 Timer 的标准适配器
 *
 * 将 @ref bm_stepper_pulse_axis_t 的 arm_timer 回调桥接至
 * @ref bm_hal_hrtimer_t。步进脉冲类型和服务接口来自 common 契约，
 * 因而本适配器不依赖任何组件头文件。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 stepper_pulse hrtimer 适配器
 * 2026-07-28       1.1            zeh            分离 app_user 与 adapter 指针透传
 * 2026-07-28       1.2            zeh            GPIO 回调改为 int 返回；可选 en_set
 * 2026-07-28       1.3            zeh            arm 遵守“只缩短”语义并保护共享状态
 * 2026-07-28       1.4            zeh            改依赖 common 步进脉冲服务契约
 */
#ifndef BM_STEPPER_PULSE_HRTIMER_ADAPTER_H
#define BM_STEPPER_PULSE_HRTIMER_ADAPTER_H

#include "bm/common/bm_stepper_pulse_service.h"
#include "bm/common/bm_types.h"
#include "hal/bm_hal_hrtimer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HRTimer 适配器实例。
 *
 * 用户静态分配；内部轴实例的布局由 common 服务契约定义。
 */
typedef struct {
    bm_stepper_pulse_axis_t axis;
    const bm_hal_hrtimer_t *hrtimer;
    uint8_t started;
    uint64_t next_deadline_us;
    void *app_user;
    int (*app_step_high)(void *user);
    int (*app_step_low)(void *user);
    int (*app_dir_set)(void *user, int level);
    int (*app_en_set)(void *user, int level);
} bm_stepper_pulse_hrtimer_adapter_t;

/**
 * @brief 初始化 HRTimer 适配器。
 *
 * 绑定 hrtimer、注册到期回调并初始化内部步进脉冲服务轴。
 *
 * @param adapter 适配器实例（用户静态分配）。
 * @param config 步进脉冲配置。
 * @param hrtimer 高精度 Timer 设备实例。
 * @param step_high STEP 拉高回调。
 * @param step_low STEP 拉低回调。
 * @param dir_set DIR 电平设置回调。
 * @param en_set EN 电平设置回调，可为 NULL。
 * @param user GPIO 回调透传参数。
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；或底层 Timer 错误码。
 */
int bm_stepper_pulse_hrtimer_adapter_init(
    bm_stepper_pulse_hrtimer_adapter_t *adapter,
    const bm_stepper_pulse_config_t *config,
    const bm_hal_hrtimer_t *hrtimer,
    int (*step_high)(void *user),
    int (*step_low)(void *user),
    int (*dir_set)(void *user, int level),
    int (*en_set)(void *user, int level),
    void *user);

/**
 * @brief 获取适配器内部的步进脉冲服务轴实例。
 * @param adapter 适配器实例。
 * @return 轴实例指针；adapter 为 NULL 时返回 NULL。
 */
bm_stepper_pulse_axis_t *bm_stepper_pulse_hrtimer_adapter_axis(
    bm_stepper_pulse_hrtimer_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* BM_STEPPER_PULSE_HRTIMER_ADAPTER_H */

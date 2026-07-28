/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse_hrtimer_adapter.h
 * @brief stepper_pulse 与高精度 Timer 的标准适配器
 *
 * 把 `bm_stepper_pulse_axis_t` 的 `arm_timer` 回调桥接到 `bm_hal_hrtimer_t`，
 * 并自动注册 Timer 到期回调。App 只需提供 STEP/DIR GPIO 回调，不再需要
 * 为每个平台自行实现 `arm_timer()`。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 stepper_pulse hrtimer 适配器
 * 2026-07-28       1.1            zeh            分离 app_user 与 adapter 指针透传
 */
#ifndef BM_STEPPER_PULSE_HRTIMER_ADAPTER_H
#define BM_STEPPER_PULSE_HRTIMER_ADAPTER_H

#include "bm/component/stepper_pulse.h"
#include "hal/bm_hal_hrtimer.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief stepper_pulse 与高精度 Timer 的适配器实例
 *
 * 用户静态分配；初始化后通过 @ref bm_stepper_pulse_hrtimer_adapter_axis()
 * 获取内部轴实例以进行调速/停止等操作。
 */
typedef struct {
    bm_stepper_pulse_axis_t     axis;       /**< 内部步进轴实例 */
    const bm_hal_hrtimer_t     *hrtimer;    /**< 绑定的高精度 Timer */
    uint8_t                     started;    /**< Timer 已启动标志 */
    void                       *app_user;   /**< App GPIO 回调透传参数 */
    void (*app_step_high)(void *user);      /**< App STEP 拉高回调 */
    void (*app_step_low)(void *user);       /**< App STEP 拉低回调 */
    void (*app_dir_set)(void *user, int level); /**< App DIR 电平回调 */
} bm_stepper_pulse_hrtimer_adapter_t;

/**
 * @brief 初始化适配器
 *
 * 绑定 hrtimer、注册到期回调、封装 `arm_timer`，并调用
 * `bm_stepper_pulse_init()` 初始化内部轴。
 *
 * @param adapter   适配器实例（用户静态分配）
 * @param config    步进脉冲配置
 * @param hrtimer   高精度 Timer 设备实例
 * @param step_high STEP 拉高回调
 * @param step_low  STEP 拉低回调
 * @param dir_set   DIR 电平设置回调
 * @param user      GPIO 回调透传参数
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_stepper_pulse_hrtimer_adapter_init(
    bm_stepper_pulse_hrtimer_adapter_t *adapter,
    const bm_stepper_pulse_config_t *config,
    const bm_hal_hrtimer_t *hrtimer,
    void (*step_high)(void *user),
    void (*step_low)(void *user),
    void (*dir_set)(void *user, int level),
    void *user);

/**
 * @brief 获取适配器内部的步进轴实例
 *
 * 用于 `bm_stepper_pulse_set_velocity()` / `bm_stepper_pulse_stop()` 等接口。
 *
 * @param adapter 适配器实例
 * @return 轴实例指针；adapter 为 NULL 时返回 NULL
 */
bm_stepper_pulse_axis_t *bm_stepper_pulse_hrtimer_adapter_axis(
    bm_stepper_pulse_hrtimer_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* BM_STEPPER_PULSE_HRTIMER_ADAPTER_H */

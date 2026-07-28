/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_estop_input.h
 * @brief 急停输入通用组件
 *
 * poll-only：支持消抖、锁存、清除；触发后由 App 决定急停/报警策略。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增急停输入通用组件
 * 2026-07-28       1.1            zeh            组件改为 poll-only，不再注册
 *                                               EXTI；同步更新 init 文档
 * 2026-07-28       1.2            zeh            改用 bm/common 防抖词汇
 */
#ifndef BM_ESTOP_INPUT_H
#define BM_ESTOP_INPUT_H

#include "hal/bm_hal_gpio.h"
#include "bm/common/bm_input_debounce.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 急停事件回调原型 */
typedef void (*bm_estop_input_callback_t)(void *user, uint32_t pin,
                                          int active, uint64_t timestamp_us);

/** @brief 配置 */
typedef struct {
    const bm_hal_gpio_t *gpio;      /**< GPIO 设备实例 */
    uint32_t             pin;       /**< pin 编码 */
    int                  active_low;/**< 非零：低电平有效 */
    uint32_t             stable_us; /**< 消抖稳定时间（µs），须 > 0 */
} bm_estop_input_config_t;

/** @brief 资源回调 */
typedef struct {
    bm_estop_input_callback_t estop_cb; /**< 触发回调；NULL 静默跳过 */
    void                     *user;     /**< 回调透传参数 */
} bm_estop_input_resources_t;

/** @brief 状态 */
typedef struct {
    bm_input_debounce_t       debounce;      /**< 消抖实例 */
    int                       active;        /**< 当前消抖后激活状态 */
    int                       latched;       /**< 锁存触发状态 */
    uint64_t                  last_event_us; /**< 最近事件时间戳 */
    uint32_t                  event_count;   /**< 事件计数 */
} bm_estop_input_state_t;

/** @brief 实例 */
typedef struct {
    bm_estop_input_config_t    config;
    bm_estop_input_resources_t resources;
    bm_estop_input_state_t     state;
} bm_estop_input_t;

/**
 * @brief 校验配置
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_estop_input_validate_config(const bm_estop_input_config_t *config);

/**
 * @brief 初始化急停输入（配置 GPIO 输入；poll-only，不注册 EXTI）
 * @param estop 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；其他平台错误码
 */
int bm_estop_input_init(bm_estop_input_t *estop);

/**
 * @brief 复位状态
 * @param estop 实例指针；NULL 静默返回
 */
void bm_estop_input_reset(bm_estop_input_t *estop);

/**
 * @brief 清除锁存
 * @param estop 实例指针；NULL 静默返回
 */
void bm_estop_input_clear_latch(bm_estop_input_t *estop);

/**
 * @brief 查询当前激活状态
 * @param estop 实例指针
 * @return 1 激活；0 未激活；NULL 时返回 0
 */
int bm_estop_input_active(const bm_estop_input_t *estop);

/**
 * @brief 查询锁存状态
 * @param estop 实例指针
 * @return 1 已锁存；0 未锁存；NULL 时返回 0
 */
int bm_estop_input_latched(const bm_estop_input_t *estop);

/**
 * @brief 周期轮询：完成消抖并触发稳定事件回调
 * @param estop 实例指针；NULL 静默返回
 */
void bm_estop_input_poll(bm_estop_input_t *estop);

#ifdef __cplusplus
}
#endif

#endif /* BM_ESTOP_INPUT_H */

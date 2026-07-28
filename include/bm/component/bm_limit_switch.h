/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_limit_switch.h
 * @brief 限位开关通用输入组件
 *
 * 架在 GPIO EXTI 上，支持消抖、事件时间戳、锁存与清除。
 * 组件只上报事件，App 决定是否急停/回退/报警。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增限位开关通用输入组件
 * 2026-07-28       1.1            zeh            注明由 EXTI 沿推导有效电平：
 *                                               FALLING 为低电平有效，
 *                                               BOTH 维持高电平有效
 * 2026-07-28       1.2            zeh            改用 bm/common 防抖词汇
 */
#ifndef BM_LIMIT_SWITCH_H
#define BM_LIMIT_SWITCH_H

#include "hal/bm_hal_gpio.h"
#include "bm/common/bm_input_debounce.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 限位开关事件回调原型 */
typedef void (*bm_limit_switch_callback_t)(void *user, uint32_t pin,
                                           int level, uint64_t timestamp_us);

/** @brief 配置（用户填写） */
typedef struct {
    const bm_hal_gpio_t *gpio;   /**< GPIO 设备实例 */
    uint32_t             pin;    /**< pin 编码 */
    uint32_t             flags;  /**< EXTI 边沿标志（RISING/FALLING/BOTH）；
                                  *   FALLING 单独配置表示低电平有效
                                  *   （常闭接地开关），RISING 与 BOTH
                                  *   为高电平有效 */
    uint32_t             stable_us; /**< 消抖稳定时间（µs），0 表示不消抖 */
} bm_limit_switch_config_t;

/** @brief 资源回调（用户填写） */
typedef struct {
    bm_limit_switch_callback_t event_cb; /**< 事件回调；NULL 静默跳过 */
    void                      *user;     /**< 回调透传参数 */
} bm_limit_switch_resources_t;

/** @brief 状态（组件维护） */
typedef struct {
    bm_input_debounce_t       debounce;     /**< 消抖实例 */
    int                       triggered;    /**< 当前是否触发 */
    int                       latched;      /**< 锁存触发状态 */
    uint64_t                  last_event_us;/**< 最近事件时间戳 */
    uint32_t                  event_count;  /**< 事件计数 */
} bm_limit_switch_state_t;

/** @brief 限位开关实例（用户静态分配） */
typedef struct {
    bm_limit_switch_config_t    config;
    bm_limit_switch_resources_t resources;
    bm_limit_switch_state_t     state;
} bm_limit_switch_t;

/**
 * @brief 校验配置
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_limit_switch_validate_config(const bm_limit_switch_config_t *config);

/**
 * @brief 初始化限位开关（配置 GPIO EXTI、注册回调）
 * @param ls 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；其他平台错误码
 */
int bm_limit_switch_init(bm_limit_switch_t *ls);

/**
 * @brief 复位状态（不清除 EXTI 配置）
 * @param ls 实例指针；NULL 静默返回
 */
void bm_limit_switch_reset(bm_limit_switch_t *ls);

/**
 * @brief 清除锁存状态
 * @param ls 实例指针；NULL 静默返回
 */
void bm_limit_switch_clear_latch(bm_limit_switch_t *ls);

/**
 * @brief 查询当前触发状态
 * @param ls 实例指针
 * @return 1 触发；0 未触发；NULL 时返回 0
 */
int bm_limit_switch_triggered(const bm_limit_switch_t *ls);

/**
 * @brief 查询锁存状态
 * @param ls 实例指针
 * @return 1 已锁存触发；0 未锁存；NULL 时返回 0
 */
int bm_limit_switch_latched(const bm_limit_switch_t *ls);

/**
 * @brief 周期轮询：完成消抖并触发稳定事件回调
 *
 * 当 config.stable_us > 0 时，业务须周期性调用本函数；
 * stable_us == 0 时事件在 EXTI 回调直接触发，本函数无需调用。
 *
 * @param ls 实例指针；NULL 静默返回
 */
void bm_limit_switch_poll(bm_limit_switch_t *ls);

#ifdef __cplusplus
}
#endif

#endif /* BM_LIMIT_SWITCH_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tmc_diag.h
 * @brief TMC DIAG 通用输入组件
 *
 * 监听 TMC 驱动器 DIAG 引脚（通常为开漏、低电平有效），EXTI 触发后锁存，
 * 由 App 决定停机/报警策略。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 TMC DIAG 通用输入组件
 */
#ifndef BM_TMC_DIAG_H
#define BM_TMC_DIAG_H

#include "hal/bm_hal_gpio.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief DIAG 事件回调原型 */
typedef void (*bm_tmc_diag_callback_t)(void *user, uint32_t pin,
                                       uint64_t timestamp_us);

/** @brief 配置 */
typedef struct {
    const bm_hal_gpio_t *gpio;   /**< GPIO 设备实例 */
    uint32_t             pin;    /**< pin 编码 */
    int                  active_low; /**< 非零：低电平有效；0：高电平有效 */
} bm_tmc_diag_config_t;

/** @brief 资源回调 */
typedef struct {
    bm_tmc_diag_callback_t diag_cb; /**< DIAG 触发回调；NULL 静默跳过 */
    void                  *user;    /**< 回调透传参数 */
} bm_tmc_diag_resources_t;

/** @brief 状态 */
typedef struct {
    int      active;        /**< 当前是否处于激活电平 */
    int      latched;       /**< 锁存触发状态 */
    uint64_t last_event_us; /**< 最近事件时间戳 */
    uint32_t event_count;   /**< 事件计数 */
} bm_tmc_diag_state_t;

/** @brief 实例 */
typedef struct {
    bm_tmc_diag_config_t    config;
    bm_tmc_diag_resources_t resources;
    bm_tmc_diag_state_t     state;
} bm_tmc_diag_t;

/**
 * @brief 校验配置
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_tmc_diag_validate_config(const bm_tmc_diag_config_t *config);

/**
 * @brief 初始化 DIAG 输入（配置 GPIO 为输入、注册 EXTI 下降沿/上升沿）
 * @param diag 实例指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法；其他平台错误码
 */
int bm_tmc_diag_init(bm_tmc_diag_t *diag);

/**
 * @brief 复位状态（不清除 EXTI 配置）
 * @param diag 实例指针；NULL 静默返回
 */
void bm_tmc_diag_reset(bm_tmc_diag_t *diag);

/**
 * @brief 清除锁存
 * @param diag 实例指针；NULL 静默返回
 */
void bm_tmc_diag_clear_latch(bm_tmc_diag_t *diag);

/**
 * @brief 查询当前激活状态
 * @param diag 实例指针
 * @return 1 激活；0 未激活；NULL 时返回 0
 */
int bm_tmc_diag_active(const bm_tmc_diag_t *diag);

/**
 * @brief 查询锁存状态
 * @param diag 实例指针
 * @return 1 已锁存；0 未锁存；NULL 时返回 0
 */
int bm_tmc_diag_latched(const bm_tmc_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* BM_TMC_DIAG_H */

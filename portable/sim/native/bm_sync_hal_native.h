/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sync_hal_native.h
 * @brief 原生仿真同步域 HAL 测试辅助接口
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#ifndef BM_SYNC_HAL_NATIVE_H
#define BM_SYNC_HAL_NATIVE_H

#include "bm_sync.h"

/**
 * @brief 重置同步 HAL 仿真状态与故障注入。
 */
void bm_sync_hal_native_reset(void);
/**
 * @brief 设置同步配置阶段的故障注入开关。
 * @param enabled 非 0 表示启用故障注入，0 表示禁用。
 */
void bm_sync_hal_native_fail_configure(int enabled);
/**
 * @brief 设置同步预备阶段的故障注入开关。
 * @param enabled 非 0 表示启用故障注入，0 表示禁用。
 */
void bm_sync_hal_native_fail_arm(int enabled);
/**
 * @brief 设置同步触发阶段的故障注入开关。
 * @param enabled 非 0 表示启用故障注入，0 表示禁用。
 */
void bm_sync_hal_native_fail_trigger(int enabled);
/**
 * @brief 查询同步 HAL 是否已触发。
 * @return 条件成立时返回非 0，否则返回 0。
 */
int bm_sync_hal_native_triggered(void);
/**
 * @brief 读取同步 HAL 执行安全停止的次数。
 * @return 已执行安全停止的次数。
 */
int bm_sync_hal_native_safe_stop_count(void);
/**
 * @brief 读取同步 HAL 对应阶段观测到的状态。
 * @return 该阶段观测到的 bm_sync_state_t 状态。
 */
bm_sync_state_t bm_sync_hal_native_configure_observed_state(void);
/**
 * @brief 读取同步 HAL 对应阶段观测到的状态。
 * @return 该阶段观测到的 bm_sync_state_t 状态。
 */
bm_sync_state_t bm_sync_hal_native_arm_observed_state(void);
/**
 * @brief 读取同步 HAL 对应阶段观测到的状态。
 * @return 该阶段观测到的 bm_sync_state_t 状态。
 */
bm_sync_state_t bm_sync_hal_native_trigger_observed_state(void);
/**
 * @brief 读取同步 HAL 对应阶段观测到的状态。
 * @return 该阶段观测到的 bm_sync_state_t 状态。
 */
bm_sync_state_t bm_sync_hal_native_safe_stop_observed_state(void);

#endif /* BM_SYNC_HAL_NATIVE_H */

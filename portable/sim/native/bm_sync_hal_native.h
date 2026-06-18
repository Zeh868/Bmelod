/**
 * @file bm_sync_hal_native.h
 * @brief 原生仿真同步域 HAL 测试辅助接口
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_SYNC_HAL_NATIVE_H
#define BM_SYNC_HAL_NATIVE_H

#include "bm_sync.h"

void bm_sync_hal_native_reset(void);
void bm_sync_hal_native_fail_configure(int enabled);
void bm_sync_hal_native_fail_arm(int enabled);
void bm_sync_hal_native_fail_trigger(int enabled);
int bm_sync_hal_native_triggered(void);
int bm_sync_hal_native_safe_stop_count(void);
bm_sync_state_t bm_sync_hal_native_configure_observed_state(void);
bm_sync_state_t bm_sync_hal_native_arm_observed_state(void);
bm_sync_state_t bm_sync_hal_native_trigger_observed_state(void);
bm_sync_state_t bm_sync_hal_native_safe_stop_observed_state(void);

#endif /* BM_SYNC_HAL_NATIVE_H */

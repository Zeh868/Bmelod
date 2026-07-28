/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse_hrtimer_adapter.c
 * @brief stepper_pulse 与高精度 Timer 的标准适配器实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 stepper_pulse hrtimer 适配器
 * 2026-07-28       1.1            zeh            修复 GPIO user 透传；移除调试 printf
 * 2026-07-28       1.2            zeh            GPIO 回调改 int 返回；可选 en_set
 */
#include "bm/component/stepper_pulse_hrtimer_adapter.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief GPIO 回调包装：STEP 拉高
 */
static int bm_stepper_pulse_hrtimer_step_high(void *user) {
    bm_stepper_pulse_hrtimer_adapter_t *adapter = user;

    if (adapter == NULL || adapter->app_step_high == NULL) {
        return BM_ERR_INVALID;
    }
    return adapter->app_step_high(adapter->app_user);
}

/**
 * @brief GPIO 回调包装：STEP 拉低
 */
static int bm_stepper_pulse_hrtimer_step_low(void *user) {
    bm_stepper_pulse_hrtimer_adapter_t *adapter = user;

    if (adapter == NULL || adapter->app_step_low == NULL) {
        return BM_ERR_INVALID;
    }
    return adapter->app_step_low(adapter->app_user);
}

/**
 * @brief GPIO 回调包装：DIR 电平设置
 */
static int bm_stepper_pulse_hrtimer_dir_set(void *user, int level) {
    bm_stepper_pulse_hrtimer_adapter_t *adapter = user;

    if (adapter == NULL || adapter->app_dir_set == NULL) {
        return BM_ERR_INVALID;
    }
    return adapter->app_dir_set(adapter->app_user, level);
}

/**
 * @brief GPIO 回调包装：EN 电平设置（可选）
 */
static int bm_stepper_pulse_hrtimer_en_set(void *user, int level) {
    bm_stepper_pulse_hrtimer_adapter_t *adapter = user;

    if (adapter == NULL || adapter->app_en_set == NULL) {
        return BM_ERR_NOT_SUPPORTED;
    }
    return adapter->app_en_set(adapter->app_user, level);
}

/**
 * @brief arm_timer 回调：桥接到高精度 Timer
 *
 * interval_us == 0 时停止 Timer；否则首次或 ONESHOT 到期后重新启动，
 * 运行中仅更新比较值。
 */
static int bm_stepper_pulse_hrtimer_arm(void *user, uint32_t interval_us) {
    bm_stepper_pulse_hrtimer_adapter_t *adapter = user;
    int rc;

    if (adapter == NULL || adapter->hrtimer == NULL) {
        return BM_ERR_INVALID;
    }

    if (interval_us == 0u) {
        rc = bm_hal_hrtimer_stop(adapter->hrtimer);
        if (rc == BM_OK) {
            adapter->started = 0;
        }
        return rc;
    }

    if (adapter->started == 0u) {
        rc = bm_hal_hrtimer_start(adapter->hrtimer,
                                  BM_HRTIMER_MODE_ONESHOT, interval_us);
        if (rc == BM_OK) {
            adapter->started = 1;
        }
        return rc;
    }

    return bm_hal_hrtimer_set_compare(adapter->hrtimer, interval_us);
}

/**
 * @brief 高精度 Timer 到期回调：驱动 stepper_pulse
 */
static void bm_stepper_pulse_hrtimer_callback(const bm_hal_hrtimer_t *dev,
                                              void *user) {
    bm_stepper_pulse_hrtimer_adapter_t *adapter = user;

    (void)dev;
    if (adapter == NULL) {
        return;
    }

    /* ONESHOT 到期后 Timer 已停止，置标志让下一次 arm_timer 重新 start */
    adapter->started = 0;
    bm_stepper_pulse_on_timer(&adapter->axis);
}

int bm_stepper_pulse_hrtimer_adapter_init(
    bm_stepper_pulse_hrtimer_adapter_t *adapter,
    const bm_stepper_pulse_config_t *config,
    const bm_hal_hrtimer_t *hrtimer,
    int (*step_high)(void *user),
    int (*step_low)(void *user),
    int (*dir_set)(void *user, int level),
    int (*en_set)(void *user, int level),
    void *user) {
    int rc;

    if (adapter == NULL || config == NULL || hrtimer == NULL
        || step_high == NULL || step_low == NULL || dir_set == NULL) {
        return BM_ERR_INVALID;
    }

    (void)memset(adapter, 0, sizeof(*adapter));
    adapter->hrtimer = hrtimer;

    adapter->app_user      = user;
    adapter->app_step_high = step_high;
    adapter->app_step_low  = step_low;
    adapter->app_dir_set   = dir_set;
    adapter->app_en_set    = en_set;

    adapter->axis.config = *config;
    adapter->axis.resources.step_high = bm_stepper_pulse_hrtimer_step_high;
    adapter->axis.resources.step_low  = bm_stepper_pulse_hrtimer_step_low;
    adapter->axis.resources.dir_set   = bm_stepper_pulse_hrtimer_dir_set;
    adapter->axis.resources.en_set    = (en_set != NULL)
        ? bm_stepper_pulse_hrtimer_en_set : NULL;
    adapter->axis.resources.arm_timer = bm_stepper_pulse_hrtimer_arm;
    adapter->axis.resources.user      = adapter;

    rc = bm_stepper_pulse_init(&adapter->axis);
    if (rc != BM_OK) {
        return rc;
    }

    rc = bm_hal_hrtimer_set_callback(hrtimer,
                                     bm_stepper_pulse_hrtimer_callback, adapter);
    if (rc != BM_OK) {
        (void)bm_stepper_pulse_reset(&adapter->axis);
        return rc;
    }

    return BM_OK;
}

bm_stepper_pulse_axis_t *bm_stepper_pulse_hrtimer_adapter_axis(
    bm_stepper_pulse_hrtimer_adapter_t *adapter) {
    if (adapter == NULL) {
        return NULL;
    }
    return &adapter->axis;
}

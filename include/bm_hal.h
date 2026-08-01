/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal.h
 * @brief HAL 契约聚合入口（核心外设；按需裁剪）
 *
 * 也可单独 `#include "hal/bm_hal_uart.h"` 等。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-08-01       1.0            zeh           补全 Doxygen 合规注释
 */
#ifndef BM_HAL_H
#define BM_HAL_H

#include "hal/bm_hal_critical.h"
#include "hal/bm_hal_memory.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_hrtimer.h"   /* 接口批 1：高精度 Timer 设备 */
#include "hal/bm_hal_uart.h"
#include "hal/bm_hal_gpio.h"      /* 接口批 1：GPIO 设备 */
#include "hal/bm_hal_console.h"
#include "hal/bm_hal_wdg.h"

#endif /* BM_HAL_H */

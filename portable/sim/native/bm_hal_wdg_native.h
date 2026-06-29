/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_wdg_native.h
 * @brief 原生仿真看门狗测试辅助接口
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

#ifndef BM_HAL_WDG_NATIVE_H
#define BM_HAL_WDG_NATIVE_H

#include <stdint.h>

/** 查询仿真 HAL 喂狗次数 */
uint32_t bm_hal_wdg_native_get_feed_count(void);

/** 清零仿真 HAL 喂狗计数 */
void bm_hal_wdg_native_reset_feed_count(void);

#endif /* BM_HAL_WDG_NATIVE_H */

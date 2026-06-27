/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_wdg.h
 * @brief 硬件看门狗 HAL 接口
 *
 * 初始化超时时间并喂狗，由平台实现具体寄存器操作。
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
#ifndef BM_HAL_WDG_H
#define BM_HAL_WDG_H

#include <stdint.h>

/**
 * @brief 初始化硬件看门狗
 *
 * @param timeout_ms 超时时间（毫秒）
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_wdg_init(uint32_t timeout_ms);

/**
 * @brief 喂硬件看门狗
 */
void bm_hal_wdg_feed(void);

#endif

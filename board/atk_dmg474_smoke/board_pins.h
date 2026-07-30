/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file board_pins.h
 * @brief ATK-DMG474 板级引脚宏（冒烟工程）
 *
 * LED 脚位请对照正点原子原理图核对；当前按常见灌电流 LED 占位 PF9。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            新增；LED 脚位标 VERIFY
 */
#ifndef ATK_DMG474_BOARD_PINS_H
#define ATK_DMG474_BOARD_PINS_H

#include "drv/bm_drv_gpio.h"

/**
 * @brief 是否驱动板载 LED（1=启用；0=仅 RTT）
 *
 * 引脚未核验前可先置 0，不影响 RTT 验收。
 */
#ifndef ATK_DMG474_LED_ENABLE
#define ATK_DMG474_LED_ENABLE 1
#endif

/**
 * @brief LED0 pin 编码（VERIFY：原理图核对前占位 PF9）
 *
 * port: 0=A .. 5=F；低电平点亮（灌电流）时 ATK_DMG474_LED_ACTIVE_LOW=1。
 */
#ifndef ATK_DMG474_LED0_PIN
#define ATK_DMG474_LED0_PIN BM_GPIO_PIN_ENCODE(5u, 9u)
#endif

#ifndef ATK_DMG474_LED_ACTIVE_LOW
#define ATK_DMG474_LED_ACTIVE_LOW 1
#endif

#endif /* ATK_DMG474_BOARD_PINS_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file main.c
 * @brief ATK-DMG474（STM32G474VET6）真机冒烟：RTT 心跳 + 可选 LED
 *
 * 成功标准：J-Link 烧写后，SEGGER RTT Viewer Channel 0 每秒可见 uptime。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-30
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            新增 ATK-DMG474 RTT 冒烟 main
 */
#include "board_pins.h"

#include "bm/common/bm_log.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_uptime.h"
#include "hal/bm_hal_console.h"
#include "hal/bm_hal_gpio.h"

#include "bm_vendor_gpio_stm32g4.h"

#include "stm32g4xx.h"

#include <stdint.h>

extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);

/**
 * @brief 粗延时（基于 SystemCoreClock 的忙等；冒烟专用，非实时）
 *
 * @param ms 毫秒
 */
static void smoke_delay_ms(uint32_t ms)
{
    uint32_t cycles_per_ms;
    uint32_t i;
    uint32_t j;

    cycles_per_ms = SystemCoreClock / 1000u;
    if (cycles_per_ms == 0u) {
        cycles_per_ms = 16000u; /* HSI 兜底 */
    }
    for (i = 0u; i < ms; i++) {
        for (j = 0u; j < (cycles_per_ms / 4u); j++) {
            __NOP();
        }
    }
}

/**
 * @brief 可选：配置并点灭 LED0
 */
static void smoke_led_init(void)
{
#if ATK_DMG474_LED_ENABLE
    (void)bm_hal_gpio_configure(&bm_stm32g4_gpio, ATK_DMG474_LED0_PIN,
                                BM_GPIO_OUTPUT);
#if ATK_DMG474_LED_ACTIVE_LOW
    (void)bm_hal_gpio_write(&bm_stm32g4_gpio, ATK_DMG474_LED0_PIN, 1);
#else
    (void)bm_hal_gpio_write(&bm_stm32g4_gpio, ATK_DMG474_LED0_PIN, 0);
#endif
#else
    /* LED 关闭时无操作 */
#endif
}

/**
 * @brief 翻转 LED0（若启用）
 */
static void smoke_led_toggle(void)
{
#if ATK_DMG474_LED_ENABLE
    (void)bm_hal_gpio_toggle(&bm_stm32g4_gpio, ATK_DMG474_LED0_PIN);
#endif
}

/**
 * @brief 冒烟入口
 *
 * SystemInit 已由 startup 调用（默认 HSI）。此处更新 SystemCoreClock，
 * 初始化 RTT console，再周期打印 uptime。
 */
int main(void)
{
    uint32_t beat = 0u;

    SystemCoreClockUpdate();

    if (bm_hal_console_init() != BM_OK) {
        /* console 失败时仍进入循环，便于调试器观察 */
    }

    smoke_led_init();

    BM_LOGI("atk", "ATK-DMG474 smoke start (G474VE, RTT)");
    BM_LOGI("atk", "SystemCoreClock=%lu Hz",
            (unsigned long)SystemCoreClock);

    for (;;) {
        uint64_t us = bm_uptime_us();
        BM_LOGI("atk", "heartbeat beat=%lu uptime_ms=%lu",
                (unsigned long)beat,
                (unsigned long)(us / 1000ull));
        smoke_led_toggle();
        beat++;
        smoke_delay_ms(1000u);
    }
}

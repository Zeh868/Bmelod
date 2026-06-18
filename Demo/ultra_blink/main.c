/**
 * @file main.c
 * @brief Ultra 极简域示例：定时翻转 LED 与按钮事件
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
#include "bm_ultra.h"
#include "hal/bm_hal_uart.h"
#include "bm_log.h"
#include "example_support.h"

#define TAG "ultra_blink"

/** 定时 tick 事件 */
#define EVENT_TICK          1
/** 按钮按下事件 */
#define EVENT_BUTTON_PRESS  2

static uint8_t g_led_state = 0;
static uint32_t g_tick_count = 0;

/** tick 回调：翻转 LED 状态并打印 */
static void on_tick(const void *data, uint8_t len) {
    (void)data;
    (void)len;
    g_led_state = !g_led_state;
    BM_LOGD(TAG, "LED %s", g_led_state ? "ON" : "OFF");
    example_print(g_led_state ? "LED: ON\n" : "LED: OFF\n");
}

/** 按钮回调 */
static void on_button(const void *data, uint8_t len) {
    (void)data;
    (void)len;
    BM_LOGI(TAG, "button pressed");
    example_print("BUTTON: pressed\n");
}

BM_ULTRA_CALLBACK_TABLE_DEFINE(
    BM_ULTRA_CB(EVENT_TICK, on_tick),
    BM_ULTRA_CB(EVENT_BUTTON_PRESS, on_button)
);

int main(void) {
    bm_hal_uart_init(NULL);
    bm_ultra_init();
    example_print("Bmelod Example: ultra_blink\n");
    BM_LOGI(TAG, "ultra_blink example start");

    uint32_t button_counter = 0;
    uint8_t pass_sent = 0;

    while (1) {
        example_delay_cycles(1000000U);
        g_tick_count++;
        button_counter++;

        bm_ultra_publish(EVENT_TICK, NULL, 0);

        if (button_counter >= 3) {
            uint8_t btn_id = 0;
            bm_ultra_publish(EVENT_BUTTON_PRESS, &btn_id, sizeof(btn_id));
            button_counter = 0;
        }

        bm_ultra_process();

        if (g_tick_count >= 3 && !pass_sent) {
            BM_LOGI(TAG, "example PASS at tick %u", (unsigned)g_tick_count);
            example_print("EXAMPLE_ULTRA: PASS\n");
            pass_sent = 1;
        }
    }
}

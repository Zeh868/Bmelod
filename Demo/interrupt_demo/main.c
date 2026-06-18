/**
 * @file main.c
 * @brief 中断驱动示例：SysTick + TIMER1 ISR 发布事件
 */
#include "app_events.h"
#include "bm_core.h"
#include "bm_event.h"
#include "hal/bm_hal_uart.h"
#include "bm_log.h"
#include "bm_module.h"
#include "example_support.h"

#define TAG "irq_demo"

static volatile uint16_t s_next_temperature = 25;

void irq_publish_tick_from_isr(void) {
    uint32_t tick = g_tick_count + 1U;
    if (bm_event_publish_copy_from_isr(
            EVENT_TICK, 0, &tick, sizeof(tick)) == BM_OK) {
        g_tick_count = tick;
    }
}

void irq_publish_sample_from_isr(void) {
    uint16_t temperature = s_next_temperature++;
    if (bm_event_publish_copy_from_isr(
            EVENT_SAMPLE, 1, &temperature, sizeof(temperature)) == BM_OK) {
        g_sample_count++;
    }
}

int main(void) {
    uint8_t stats_sent = 0;
    uint8_t pass_sent = 0;

    bm_hal_uart_init(NULL);
    example_print("Bmelod Example: interrupt_demo\n");
    BM_LOGI(TAG, "interrupt_demo example start");

    if (bm_module_boot() != BM_OK) {
        BM_LOGE(TAG, "app init failed");
        example_print("EXAMPLE_IRQ: INIT FAILED\n");
        return 1;
    }

    while (1) {
        bm_event_process(8);

        if (g_tick_count >= 20U && g_sample_count >= 2U && !stats_sent) {
            if (bm_event_publish_copy(EVENT_STATS, 2, NULL, 0) == BM_OK) {
                BM_LOGI(TAG, "stats event published: ticks=%u samples=%u",
                        (unsigned)g_tick_count, (unsigned)g_sample_count);
                stats_sent = 1;
            }
        }

        if (stats_sent && g_sample_count >= 2U && !pass_sent) {
            BM_LOGI(TAG, "example PASS");
            example_print("EXAMPLE_IRQ: PASS\n");
            pass_sent = 1;
        }
    }
}

/**
 * @file main.c
 * @brief 全系统集成示例：模块、事件风暴优先级、主循环喂狗与故障处理
 */
#include "app_events.h"
#include "hal/bm_hal_uart.h"
#include "example_support.h"
#include "bmelod.h"

#define TAG "full_system"

typedef struct {
    uint16_t temp;
    uint16_t humidity;
    uint32_t timestamp;
    uint8_t status;
} sensor_data_t;

BM_MEMPOOL_DEFINE(sensor_pool, sensor_data_t, 4);

static uint32_t sensor_cycle;

static int publish_sensor_sample(void) {
    sensor_data_t *sample = bm_mempool_alloc(&sensor_pool);
    if (!sample) {
        BM_LOGW(TAG, "mempool alloc failed at cycle %u", (unsigned)sensor_cycle);
        return BM_ERR_NO_MEM;
    }

    sample->temp = (uint16_t)(22U + sensor_cycle % 5U);
    sample->humidity = (uint16_t)(44U + sensor_cycle % 5U);
    sample->timestamp = sensor_cycle;
    sample->status = 0;

    int rc = bm_event_publish_copy(EVENT_TEMP, 1, sample, sizeof(*sample));
    bm_mempool_free(&sensor_pool, sample);
    return rc;
}

static int publish_storm(void) {
    static const bm_event_priority_t priorities[EVENT_STORM_COUNT] = {
        2, 0, 3, 1, 0
    };

    BM_LOGI(TAG, "publishing event storm");
    for (uint8_t i = 0; i < EVENT_STORM_COUNT; i++) {
        int rc = bm_event_publish_copy(
            (bm_event_type_t)(EVENT_STORM_BASE + i),
            priorities[i],
            NULL,
            0);
        if (rc != BM_OK) {
            return rc;
        }
    }
    return BM_OK;
}

int main(void) {
    uint8_t storm_published = 0;
    uint8_t pass_sent = 0;

    bm_hal_uart_init(NULL);
    example_print("Bmelod Example: full_system\n");
    BM_LOGI(TAG, "full_system example start");

    if (bm_module_boot() != BM_OK) {
        BM_LOGE(TAG, "app init failed");
        example_print("EXAMPLE_FULL: INIT FAILED\n");
        return 1;
    }

    example_print("[wdg] main-loop hardware watchdog\n");
    BM_LOGI(TAG, "hardware watchdog via main loop");

    while (1) {
        example_delay_cycles(1000000U);
        sensor_cycle++;

        if (publish_sensor_sample() != BM_OK) {
            example_print("[sensor] sample dropped\n");
        }

        if (sensor_cycle == 3U && !storm_published) {
            storm_published = (publish_storm() == BM_OK);
        }

        if (sensor_cycle == 5U && !app_fault_observed_get()) {
            uint8_t code = 3;
            bm_event_publish_copy(EVENT_SENSOR_FAULT, 0, &code, sizeof(code));
        }

        bm_event_process(8);

        if (storm_published && app_storm_count_get() == EVENT_STORM_COUNT) {
            example_print(app_storm_order_is_valid()
                ? "[storm] priority order verified\n"
                : "[storm] priority order failed\n");
            BM_LOGI(TAG, "storm priority order %s",
                    app_storm_order_is_valid() ? "verified" : "failed");
            storm_published = 0;
        }

        bm_wdg_feed();

        if (app_storm_count_get() == EVENT_STORM_COUNT &&
            app_storm_order_is_valid() &&
            app_fault_observed_get() &&
            sensor_cycle >= 6U &&
            !pass_sent) {
            BM_LOGI(TAG, "example PASS at cycle %u", (unsigned)sensor_cycle);
            example_print("EXAMPLE_FULL: PASS\n");
            pass_sent = 1;
        }
    }
}

/**
 * @file main.c
 * @brief 核心域传感器示例：模块生命周期 + 事件发布/订阅
 */
#include "app_events.h"
#include "bm_core.h"
#include "hal/bm_hal_uart.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_mempool.h"
#include "example_support.h"

#define TAG "core_sensor"

typedef struct {
    uint16_t temp;
    uint16_t humidity;
    uint32_t timestamp;
    uint8_t status;
} sensor_data_t;

BM_MEMPOOL_DEFINE(sensor_pool, sensor_data_t, 4);

static int publish_sensor_sample(uint32_t cycle) {
    sensor_data_t *sample = bm_mempool_alloc(&sensor_pool);
    if (!sample) {
        BM_LOGW(TAG, "mempool alloc failed at cycle %u", (unsigned)cycle);
        return BM_ERR_NO_MEM;
    }

    sample->temp = (uint16_t)(23U + cycle / 2U);
    sample->humidity = (uint16_t)(45U + cycle / 2U);
    sample->timestamp = cycle;
    sample->status = 0;

    int rc = bm_event_publish_copy(EVENT_TEMP, 1, sample, sizeof(*sample));
    bm_mempool_free(&sensor_pool, sample);
    return rc;
}

int main(void) {
    bm_hal_uart_init(NULL);
    example_print("Bmelod Example: core_sensor\n");
    BM_LOGI(TAG, "core_sensor example start");

    if (bm_module_boot() != BM_OK) {
        BM_LOGE(TAG, "app init failed");
        example_print("EXAMPLE_CORE: INIT FAILED\n");
        return 1;
    }

    uint32_t cycle = 0;
    uint8_t pass_sent = 0;

    while (1) {
        example_delay_cycles(1000000U);
        cycle++;

        if ((cycle % 2U) == 0U && publish_sensor_sample(cycle) != BM_OK) {
            example_print("[sensor] sample dropped\n");
        }

        bm_event_process(4);

        if (cycle >= 4U && !pass_sent) {
            BM_LOGI(TAG, "example PASS at cycle %u", (unsigned)cycle);
            example_print("EXAMPLE_CORE: PASS\n");
            pass_sent = 1;
        }
    }
}

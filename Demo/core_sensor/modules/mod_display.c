#include "app_events.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"
#include "example_support.h"

#define TAG "core_sensor"

static bm_event_subscriber_id_t s_sub_id;

typedef struct {
    uint16_t temp;
    uint16_t humidity;
    uint32_t timestamp;
    uint8_t status;
} sensor_data_t;

static void display_on_temp(const bm_event_t *event, void *user_data) {
    const sensor_data_t *data = event->data;
    (void)user_data;
    example_print("[display] temperature=");
    example_print_u32(data->temp);
    example_print(", humidity=");
    example_print_u32(data->humidity);
    example_print("\n");
}

static int display_init(void) {
    example_print("[mod] display init\n");
    BM_LOGI(TAG, "module display init");
    return BM_OK;
}

static int display_start(void) {
    return bm_event_subscribe(EVENT_TEMP, display_on_temp, NULL, &s_sub_id);
}

static int display_stop(void) {
    return bm_event_unsubscribe(EVENT_TEMP, s_sub_id);
}

BM_MODULE_DEFINE(display, 1, display_init, display_start, display_stop, NULL);

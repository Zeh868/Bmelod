#include "app_events.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"
#include "example_support.h"

#define TAG "full_system"

static bm_event_subscriber_id_t s_fault_sub;
static bm_event_subscriber_id_t s_storm_sub;

static void fault_on_event(const bm_event_t *event, void *user_data) {
    const uint8_t *code = event->data;
    (void)user_data;

    example_print("[fault] sensor code=");
    example_print_u32(*code);
    example_print("\n");
    BM_LOGW(TAG, "sensor fault code=%u", (unsigned)*code);
    app_fault_observed_set();
}

static void storm_on_event(const bm_event_t *event, void *user_data) {
    (void)user_data;
    app_storm_record_priority(event->priority);
}

static int fault_init(void) {
    example_print("[mod] fault init\n");
    BM_LOGI(TAG, "module fault init");
    return BM_OK;
}

static int fault_start(void) {
    if (bm_event_subscribe(EVENT_SENSOR_FAULT, fault_on_event, NULL,
                           &s_fault_sub) != BM_OK) {
        return BM_ERR_INVALID;
    }

    for (uint8_t i = 0; i < EVENT_STORM_COUNT; i++) {
        bm_event_type_t type = (bm_event_type_t)(EVENT_STORM_BASE + i);
        if (bm_event_subscribe(type, storm_on_event, NULL, &s_storm_sub) != BM_OK) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

BM_MODULE_DEFINE(fault, 1, fault_init, fault_start, NULL, NULL);

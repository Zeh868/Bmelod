#include "app_events.h"
#include "bm_event.h"
#include "bm_module.h"

static int events_init(void) {
    if (bm_event_register_type(EVENT_TEMP, "TEMP") != BM_OK ||
        bm_event_register_type(EVENT_SENSOR_FAULT, "SENSOR_FAULT") != BM_OK) {
        return BM_ERR_INVALID;
    }

    for (uint8_t i = 0; i < EVENT_STORM_COUNT; i++) {
        bm_event_type_t type = (bm_event_type_t)(EVENT_STORM_BASE + i);
        if (bm_event_register_type(type, "STORM") != BM_OK) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

BM_MODULE_DEFINE(events, 0, events_init, NULL, NULL, NULL);

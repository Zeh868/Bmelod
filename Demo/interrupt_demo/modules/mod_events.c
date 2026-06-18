#include "app_events.h"
#include "bm_event.h"
#include "bm_module.h"

static int events_init(void) {
    if (bm_event_register_type(EVENT_TICK, "TICK") != BM_OK ||
        bm_event_register_type(EVENT_SAMPLE, "SAMPLE") != BM_OK ||
        bm_event_register_type(EVENT_STATS, "STATS") != BM_OK) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

BM_MODULE_DEFINE(events, 0, events_init, NULL, NULL, NULL);

#include "app_events.h"
#include "bm_event.h"
#include "bm_module.h"

static int events_init(void) {
    return bm_event_register_type(EVENT_TEMP, "TEMP");
}

BM_MODULE_DEFINE(events, 0, events_init, NULL, NULL, NULL);

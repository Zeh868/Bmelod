#include "app_bms.h"
#include "bm_event.h"
#include "bm_module.h"

static bm_event_subscriber_id_t s_sub_id;

static int events_init(void) {
    return bm_event_register_type(EVENT_CELL_CHECK, "CELL_CHECK");
}

static int events_start(void) {
    return bm_event_subscribe(EVENT_CELL_CHECK, app_on_cell_check, NULL, &s_sub_id);
}

BM_MODULE_DEFINE(events, 0, events_init, events_start, NULL, NULL);

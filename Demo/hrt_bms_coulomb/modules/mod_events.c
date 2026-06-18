#include "app_bms.h"
#include "bm_event.h"
#include "bm_module.h"

static bm_event_subscriber_id_t s_sub_id;

static void on_integrate(const bm_event_t *event, void *user_data) {
    (void)user_data;
    if (event->type == TICKER_INTEGRATE) {
        app_cell_integrate_step(&g_cell_0);
    }
}

static int events_init(void) {
    return bm_event_register_type(TICKER_INTEGRATE, "INTEGRATE");
}

static int events_start(void) {
    return bm_event_subscribe(TICKER_INTEGRATE, on_integrate, NULL, &s_sub_id);
}

BM_MODULE_DEFINE(events, 0, events_init, events_start, NULL, NULL);

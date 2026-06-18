#include "app_events.h"
#include "bm_event.h"
#include "bm_module.h"
#include "example_support.h"

static bm_event_subscriber_id_t s_sub_id;

static void tick_on_event(const bm_event_t *event, void *user_data) {
    const uint32_t *tick = event->data;
    (void)user_data;

    example_print("[tick] count=");
    example_print_u32(*tick);
    example_print("\n");
}

static int tick_start(void) {
    return bm_event_subscribe(EVENT_TICK, tick_on_event, NULL, &s_sub_id);
}

BM_MODULE_DEFINE(tick, 1, NULL, tick_start, NULL, NULL);

#include "app_events.h"
#include "bm_event.h"
#include "bm_module.h"
#include "example_support.h"

static bm_event_subscriber_id_t s_sub_id;

static void stats_on_event(const bm_event_t *event, void *user_data) {
    (void)event;
    (void)user_data;

    example_print("[stats] ticks=");
    example_print_u32(g_tick_count);
    example_print(", samples=");
    example_print_u32(g_sample_count);
    example_print("\n");
}

static int stats_start(void) {
    return bm_event_subscribe(EVENT_STATS, stats_on_event, NULL, &s_sub_id);
}

BM_MODULE_DEFINE(stats, 3, NULL, stats_start, NULL, NULL);

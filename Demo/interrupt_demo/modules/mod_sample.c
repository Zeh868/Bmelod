#include "app_events.h"
#include "bm_event.h"
#include "bm_module.h"
#include "example_support.h"

static bm_event_subscriber_id_t s_sub_id;

static void sample_on_event(const bm_event_t *event, void *user_data) {
    const uint16_t *temperature = event->data;
    (void)user_data;

    example_print("[sample] temperature=");
    example_print_u32(*temperature);
    example_print("\n");
}

static int sample_start(void) {
    return bm_event_subscribe(EVENT_SAMPLE, sample_on_event, NULL, &s_sub_id);
}

BM_MODULE_DEFINE(sample, 2, NULL, sample_start, NULL, NULL);

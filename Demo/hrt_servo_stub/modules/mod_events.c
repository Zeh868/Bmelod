#include "bm_event.h"
#include "bm_module.h"

#define EVENT_POSITION 1u

typedef struct {
    uint32_t current_hits;
    uint32_t speed_hits;
    uint32_t position_events;
    uint16_t duty;
    int32_t position;
} servo_state_t;

extern servo_state_t g_servo_state;

static bm_event_subscriber_id_t s_sub_id;

static void on_position_event(const bm_event_t *event, void *user_data) {
    (void)user_data;
    if (event->type == EVENT_POSITION) {
        g_servo_state.position_events++;
    }
}

static int events_init(void) {
    return bm_event_register_type(EVENT_POSITION, "POSITION");
}

static int events_start(void) {
    return bm_event_subscribe(EVENT_POSITION, on_position_event, NULL, &s_sub_id);
}

BM_MODULE_DEFINE(events, 0, events_init, events_start, NULL, NULL);

#include "app_events.h"

static uint8_t s_fault_observed;
static uint8_t s_storm_priorities[EVENT_STORM_COUNT];
static uint8_t s_storm_event_count;

void app_fault_observed_set(void) {
    s_fault_observed = 1;
}

int app_fault_observed_get(void) {
    return (int)s_fault_observed;
}

void app_storm_record_priority(uint8_t priority) {
    if (s_storm_event_count < EVENT_STORM_COUNT) {
        s_storm_priorities[s_storm_event_count++] = priority;
    }
}

int app_storm_count_get(void) {
    return (int)s_storm_event_count;
}

int app_storm_order_is_valid(void) {
    static const uint8_t expected[EVENT_STORM_COUNT] = {0, 0, 1, 2, 3};

    if (s_storm_event_count != EVENT_STORM_COUNT) {
        return 0;
    }
    for (uint8_t i = 0; i < EVENT_STORM_COUNT; i++) {
        if (s_storm_priorities[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

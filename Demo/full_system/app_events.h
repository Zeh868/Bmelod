#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>

#define EVENT_TEMP          1
#define EVENT_SENSOR_FAULT  2
#define EVENT_STORM_BASE    8
#define EVENT_STORM_COUNT   5

void app_fault_observed_set(void);
int app_fault_observed_get(void);
void app_storm_record_priority(uint8_t priority);
int app_storm_count_get(void);
int app_storm_order_is_valid(void);

#endif

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>

#define EVENT_TICK    1
#define EVENT_SAMPLE  2
#define EVENT_STATS   3

extern volatile uint32_t g_tick_count;
extern volatile uint32_t g_sample_count;

#endif

#ifndef APP_BMS_H
#define APP_BMS_H

#include "bm_event.h"

#define CELL_COUNT       16u
#define EVENT_CELL_CHECK 2u

void app_on_cell_check(const bm_event_t *event, void *user_data);

#endif

#include "app_events.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"
#include "example_support.h"

#define TAG "full_system"

static bm_event_subscriber_id_t s_sub_id;

static void comm_on_temp(const bm_event_t *event, void *user_data) {
    (void)event;
    (void)user_data;
    example_print("[comm] sensor frame sent\n");
}

static int comm_init(void) {
    example_print("[mod] comm init\n");
    BM_LOGI(TAG, "module comm init");
    return BM_OK;
}

static int comm_start(void) {
    return bm_event_subscribe(EVENT_TEMP, comm_on_temp, NULL, &s_sub_id);
}

static int comm_stop(void) {
    return bm_event_unsubscribe(EVENT_TEMP, s_sub_id);
}

static int comm_deinit(void) {
    return BM_OK;
}

BM_MODULE_DEFINE(comm, 2, comm_init, comm_start, comm_stop, comm_deinit);

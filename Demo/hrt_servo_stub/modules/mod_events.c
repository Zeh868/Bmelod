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
    int rc = bm_event_register_type(EVENT_POSITION, "POSITION");
    if (rc != BM_OK) {
        return rc;
    }
    /* 订阅须在 init 阶段完成：bm_module_boot 的 init_all 收尾会冻结
     * 订阅表（bm_event_freeze_subscriptions），start 阶段再订阅会被
     * 拒绝（BM_ERR_BUSY），与 bus_servo supervisor 同一范式 */
    return bm_event_subscribe(EVENT_POSITION, on_position_event, NULL, &s_sub_id);
}

static int events_start(void) {
    return BM_OK;
}

BM_MODULE_DEFINE(events, 0, events_init, events_start, NULL, NULL);

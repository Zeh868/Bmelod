/**
 * @file mod_supervisor.c
 * @brief BMS 估算监督模块（SRT）：遥测轮询
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#include "app_bms_est.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "bms_est_sup"

static bm_event_subscriber_id_t s_sub_id;

static void poll_telemetry(void) {
    bm_bms_est_telemetry_t tel;

    app_bms_est_read_telemetry(&tel);
    if ((tel.status & BM_BMS_EST_TEL_VALID) != 0u) {
        g_supervisor_metrics.telemetry_reads++;
        g_supervisor_metrics.last_soc = tel.soc;
    }
}

static void on_bms_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_BMS_EST_START) {
        BM_LOGI(TAG, "estimation started");
        return;
    }

    if (event->type == EVENT_BMS_EST_POLL) {
        poll_telemetry();
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_BMS_EST_START, "BMS_START");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_BMS_EST_POLL, "BMS_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_BMS_EST_START, on_bms_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_BMS_EST_POLL, on_bms_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_BMS_EST_START, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

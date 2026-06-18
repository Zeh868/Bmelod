/**
 * @file mod_supervisor.c
 * @brief 块流监督模块（SRT）：启动生产与遥测轮询
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
#include "app_stream.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "stream_sup"

static bm_event_subscriber_id_t s_sub_id;

static void on_stream_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_STREAM_ENABLE) {
        app_stream_enable_production();
        BM_LOGI(TAG, "stream production enabled");
        return;
    }

    if (event->type == EVENT_STREAM_POLL) {
        if (g_stream_metrics.blocks_processed > 0u) {
            g_stream_metrics.telemetry_reads++;
        }
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_STREAM_ENABLE, "STR_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_STREAM_POLL, "STR_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_STREAM_ENABLE, on_stream_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_STREAM_POLL, on_stream_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_STREAM_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

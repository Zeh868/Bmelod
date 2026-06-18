/**
 * @file mod_supervisor.c
 * @brief stream_frontend 监督模块（SRT）：启停生产与遥测轮询
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始发布
 */
#include "app_stream_frontend.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "stream_fe_sup"

static bm_event_subscriber_id_t s_sub_id;

static void on_stream_fe_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_STREAM_FE_ENABLE) {
        app_stream_frontend_enable_production();
        BM_LOGI(TAG, "stream frontend production enabled");
        return;
    }

    if (event->type == EVENT_STREAM_FE_POLL) {
        if (g_stream_fe_metrics.blocks_processed > 0u) {
            g_stream_fe_metrics.telemetry_reads++;
            g_stream_fe_metrics.last_drift_ratio =
                g_stream_fe_axis.state.telemetry.drift_ratio;
            g_stream_fe_metrics.last_overrun =
                g_stream_fe_axis.state.telemetry.overrun;
            g_stream_fe_metrics.last_sequence =
                g_stream_fe_axis.state.telemetry.sequence;
            BM_LOGI(TAG, "tel drift=%.4f overrun=%u seq=%u",
                    (double)g_stream_fe_metrics.last_drift_ratio,
                    (unsigned)g_stream_fe_metrics.last_overrun,
                    (unsigned)g_stream_fe_metrics.last_sequence);
        }
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_STREAM_FE_ENABLE, "SFE_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_STREAM_FE_POLL, "SFE_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_STREAM_FE_ENABLE, on_stream_fe_event,
                            NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_STREAM_FE_POLL, on_stream_fe_event,
                            NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_STREAM_FE_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

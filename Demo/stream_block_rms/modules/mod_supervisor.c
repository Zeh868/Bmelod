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
 * 2026-08-01       1.1            zeh            事件订阅由 start 前移到 init
 *                                                 （bm_module_boot 冻结订阅表）
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
    /* 订阅须落在 init：bm_module_boot 在 init 结束后冻结订阅表，
     * start 中订阅会被拒绝（BM_ERR_BUSY），与 bus_servo 同一约定 */
    rc = bm_event_subscribe(EVENT_STREAM_ENABLE, on_stream_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_STREAM_POLL, on_stream_event, NULL, &s_sub_id);
    return rc;
}

static int supervisor_start(void) {
    (void)bm_event_publish_copy(EVENT_STREAM_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

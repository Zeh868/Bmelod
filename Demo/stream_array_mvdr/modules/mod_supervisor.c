/**
 * @file mod_supervisor.c
 * @brief 阵列 MVDR 块流监督模块（SRT）
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
#include "app_array_mvdr.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "mvdr_sup"

static bm_event_subscriber_id_t s_sub_id;

static void on_array_mvdr_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_ARRAY_MVDR_ENABLE) {
        app_array_mvdr_enable_production();
        BM_LOGI(TAG, "array mvdr stream enabled");
        return;
    }
    if (event->type == EVENT_ARRAY_MVDR_POLL) {
        if (g_array_mvdr_metrics.blocks_processed > 0u) {
            g_array_mvdr_metrics.telemetry_reads++;
        }
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_ARRAY_MVDR_ENABLE, "ARR_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_ARRAY_MVDR_POLL, "ARR_POLL");
    return rc;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_ARRAY_MVDR_ENABLE, on_array_mvdr_event,
                            NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_ARRAY_MVDR_POLL, on_array_mvdr_event,
                            NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    (void)bm_event_publish_copy(EVENT_ARRAY_MVDR_ENABLE, 1u, NULL, 0u);
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

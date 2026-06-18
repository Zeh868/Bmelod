/**
 * @file mod_vision_pipeline.c
 * @brief 视觉帧流监督模块（SRT）：启动生产与遥测轮询
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
#include "app_vision.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "vision_sup"

static bm_event_subscriber_id_t s_sub_id;

static void on_vision_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_VISION_ENABLE) {
        app_vision_enable_production();
        BM_LOGI(TAG, "vision stream enabled");
        return;
    }
    if (event->type == EVENT_VISION_POLL) {
        if (g_vision_metrics.blocks_processed > 0u) {
            g_vision_metrics.telemetry_reads++;
        }
    }
}

static int vision_pipeline_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_VISION_ENABLE, "VIS_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_VISION_POLL, "VIS_POLL");
    return rc;
}

static int vision_pipeline_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_VISION_ENABLE, on_vision_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_VISION_POLL, on_vision_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    (void)bm_event_publish_copy(EVENT_VISION_ENABLE, 1u, NULL, 0u);
    return BM_OK;
}

BM_MODULE_DEFINE(vision_pipeline, 0,
                 vision_pipeline_init, vision_pipeline_start, NULL, NULL);

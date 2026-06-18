/**
 * @file mod_supervisor.c
 * @brief 块流 FFT 监督模块（SRT）：启动与遥测轮询
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
#include "app_fft.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "fft_sup"

static bm_event_subscriber_id_t s_sub_id;

static void on_fft_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_FFT_ENABLE) {
        app_fft_enable_production();
        BM_LOGI(TAG, "FFT 块流生产已使能");
        return;
    }

    if (event->type == EVENT_FFT_POLL) {
        if (g_fft_metrics.blocks_processed > 0u) {
            g_fft_metrics.telemetry_reads++;
        }
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_FFT_ENABLE, "FFT_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_FFT_POLL, "FFT_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_FFT_ENABLE, on_fft_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_FFT_POLL, on_fft_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_FFT_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "监督层已启动");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

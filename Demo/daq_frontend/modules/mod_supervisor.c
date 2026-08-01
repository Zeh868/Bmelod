/**
 * @file mod_supervisor.c
 * @brief DAQ 块流监督模块（SRT）：启动生产与遥测轮询
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
#include "app_daq.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#define TAG "daq_sup"

static bm_event_subscriber_id_t s_sub_id;

static void on_daq_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_DAQ_ENABLE) {
        app_daq_enable_production();
        BM_LOGI(TAG, "daq stream enabled");
        return;
    }

    if (event->type == EVENT_DAQ_POLL) {
        if (g_daq_metrics.blocks_processed > 0u) {
            g_daq_metrics.telemetry_reads++;
        }
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_DAQ_ENABLE, "DAQ_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_DAQ_POLL, "DAQ_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    /* 订阅须落在 init：bm_module_boot 在 init 结束后冻结订阅表，
     * start 中订阅会被拒绝（BM_ERR_BUSY），与 bus_servo 同一约定 */
    rc = bm_event_subscribe(EVENT_DAQ_ENABLE, on_daq_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_DAQ_POLL, on_daq_event, NULL, &s_sub_id);
    return rc;
}

static int supervisor_start(void) {
    (void)bm_event_publish_copy(EVENT_DAQ_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

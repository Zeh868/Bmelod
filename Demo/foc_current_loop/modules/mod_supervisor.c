/**
 * @file mod_supervisor.c
 * @brief FOC 监督模块（SRT）：电流参考发布与遥测轮询
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
#include "app_foc.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#include <string.h>

#define TAG "foc_sup"

static bm_event_subscriber_id_t s_sub_id;
static uint32_t s_cmd_sequence;

static void publish_enable_command(float iq_ref_a) {
    foc_command_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = ++s_cmd_sequence;
    cmd.status = FOC_CMD_STATUS_ENABLED;
    cmd.id_ref_a = 0.0f;
    cmd.iq_ref_a = iq_ref_a;
    app_foc_publish_command(&cmd);
    g_supervisor_metrics.command_publishes++;
}

static void poll_telemetry(void) {
    foc_telemetry_t tel;

    app_foc_read_telemetry(&tel);
    if ((tel.status & FOC_TEL_STATUS_VALID) != 0u) {
        g_supervisor_metrics.telemetry_reads++;
    }
}

static void on_foc_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_FOC_ENABLE) {
        publish_enable_command(FOC_IQ_REF_A);
        return;
    }

    if (event->type == EVENT_FOC_SETPOINT) {
        float iq_ref = FOC_IQ_REF_A;

        if (event->data_len >= sizeof(float)) {
            memcpy(&iq_ref, event->data, sizeof(float));
        }
        publish_enable_command(iq_ref);
        return;
    }

    if (event->type == EVENT_FOC_FAULT) {
        g_supervisor_metrics.fault_events++;
        app_foc_inject_fault();
        return;
    }

    if (event->type == EVENT_FOC_POLL) {
        poll_telemetry();
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_FOC_ENABLE, "FOC_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_FOC_SETPOINT, "FOC_SP");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_FOC_FAULT, "FOC_FLT");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_FOC_POLL, "FOC_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_FOC_ENABLE, on_foc_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_FOC_SETPOINT, on_foc_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_FOC_FAULT, on_foc_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_FOC_POLL, on_foc_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_FOC_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started, iq_ref command published");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

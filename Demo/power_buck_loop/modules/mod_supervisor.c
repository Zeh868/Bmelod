/**
 * @file mod_supervisor.c
 * @brief 电源监督模块（SRT）：命令发布与遥测轮询
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
#include "app_power.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#include <string.h>

#define TAG "power_sup"

static bm_event_subscriber_id_t s_sub_id;
static uint32_t s_cmd_sequence;

static void publish_enable_command(float v_set_v) {
    bm_power_ctrl_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = ++s_cmd_sequence;
    cmd.status = BM_POWER_CTRL_CMD_ENABLED;
    cmd.v_set_v = v_set_v;
    app_power_publish_command(&cmd);
    g_supervisor_metrics.command_publishes++;
}

static void poll_telemetry(void) {
    bm_power_ctrl_telemetry_t tel;

    app_power_read_telemetry(&tel);
    if ((tel.status & BM_POWER_CTRL_TEL_VALID) != 0u) {
        g_supervisor_metrics.telemetry_reads++;
    }
}

static void on_power_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_POWER_ENABLE) {
        publish_enable_command(POWER_SETPOINT_V);
        return;
    }

    if (event->type == EVENT_POWER_SETPOINT) {
        float setpoint = POWER_SETPOINT_V;

        if (event->data_len >= sizeof(float)) {
            memcpy(&setpoint, event->data, sizeof(float));
        }
        publish_enable_command(setpoint);
        return;
    }

    if (event->type == EVENT_POWER_FAULT) {
        g_supervisor_metrics.fault_events++;
        app_power_inject_fault();
        return;
    }

    if (event->type == EVENT_POWER_POLL) {
        poll_telemetry();
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_POWER_ENABLE, "PWR_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_POWER_SETPOINT, "PWR_SP");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_POWER_FAULT, "PWR_FLT");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_POWER_POLL, "PWR_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_POWER_ENABLE, on_power_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_POWER_SETPOINT, on_power_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_POWER_FAULT, on_power_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_POWER_POLL, on_power_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_POWER_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

/**
 * @file mod_supervisor.c
 * @brief 电机监督模块（SRT）：速度设定与遥测轮询
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
#include "app_motor.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#include <string.h>

#define TAG "motor_sup"

static bm_event_subscriber_id_t s_sub_id;
static uint32_t s_cmd_sequence;

static void publish_enable_command(float speed_rad_s) {
    bm_motor_foc_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = ++s_cmd_sequence;
    cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    cmd.speed_setpoint_rad_s = speed_rad_s;
    cmd.id_ref_a = 0.0f;
    app_motor_publish_command(&cmd);
    g_supervisor_metrics.command_publishes++;
}

static void poll_telemetry(void) {
    bm_motor_foc_telemetry_t tel;

    app_motor_read_telemetry(&tel);
    if ((tel.status & BM_MOTOR_FOC_TEL_VALID) != 0u) {
        g_supervisor_metrics.telemetry_reads++;
    }
}

static void on_motor_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_MOTOR_ENABLE) {
        publish_enable_command(MOTOR_SPEED_SETPOINT_RAD_S);
        return;
    }

    if (event->type == EVENT_MOTOR_SETPOINT) {
        float speed = MOTOR_SPEED_SETPOINT_RAD_S;

        if (event->data_len >= sizeof(float)) {
            memcpy(&speed, event->data, sizeof(float));
        }
        publish_enable_command(speed);
        return;
    }

    if (event->type == EVENT_MOTOR_FAULT) {
        g_supervisor_metrics.fault_events++;
        app_motor_inject_fault();
        return;
    }

    if (event->type == EVENT_MOTOR_POLL) {
        poll_telemetry();
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_MOTOR_ENABLE, "MOTOR_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_MOTOR_SETPOINT, "MOTOR_SP");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_MOTOR_FAULT, "MOTOR_FLT");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_MOTOR_POLL, "MOTOR_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int supervisor_start(void) {
    int rc;

    rc = bm_event_subscribe(EVENT_MOTOR_ENABLE, on_motor_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_MOTOR_SETPOINT, on_motor_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_MOTOR_FAULT, on_motor_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_MOTOR_POLL, on_motor_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }

    (void)bm_event_publish_copy(EVENT_MOTOR_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

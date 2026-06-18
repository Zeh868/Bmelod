/**
 * @file mod_supervisor.c
 * @brief 伺服监督模块（SRT）：命令发布、遥测轮询与故障事件
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            订阅移至 init，对齐冻结前接线
 *
 */
#include "app_servo.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#include <string.h>

#define TAG "servo_sup"

static bm_event_subscriber_id_t s_sub_id;
static uint32_t s_cmd_sequence;

/** 组装并发布使能命令快照 */
static void publish_enable_command(float setpoint_rad_s) {
    servo_command_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = ++s_cmd_sequence;
    cmd.status = SERVO_CMD_STATUS_ENABLED;
    cmd.velocity_setpoint_rad_s = setpoint_rad_s;
    app_servo_publish_command(&cmd);
    g_supervisor_metrics.command_publishes++;
}

/** 读取 HRT 遥测快照并统计有效样本 */
static void poll_telemetry(void) {
    servo_telemetry_t tel;

    app_servo_read_telemetry(&tel);
    if ((tel.status & SERVO_TEL_STATUS_VALID) != 0u) {
        g_supervisor_metrics.telemetry_reads++;
    }
}

static void on_servo_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_SERVO_ENABLE) {
        publish_enable_command(SERVO_SETPOINT_RAD_S);
        return;
    }

    if (event->type == EVENT_SERVO_SETPOINT) {
        float setpoint = SERVO_SETPOINT_RAD_S;

        if (event->data_len >= sizeof(float)) {
            memcpy(&setpoint, event->data, sizeof(float));
        }
        publish_enable_command(setpoint);
        return;
    }

    if (event->type == EVENT_SERVO_FAULT) {
        g_supervisor_metrics.fault_events++;
        app_servo_inject_fault();
        return;
    }

    if (event->type == EVENT_SERVO_POLL) {
        poll_telemetry();
    }
}

static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_SERVO_ENABLE, "SERVO_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_SERVO_SETPOINT, "SERVO_SP");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_SERVO_FAULT, "SERVO_FLT");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_SERVO_POLL, "SERVO_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_SERVO_ENABLE, on_servo_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_SERVO_SETPOINT, on_servo_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_SERVO_FAULT, on_servo_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_SERVO_POLL, on_servo_event, NULL, &s_sub_id);
    return rc;
}

static int supervisor_start(void) {
    /* 启动后发布使能事件，由本模块回调写入命令快照 */
    (void)bm_event_publish_copy(EVENT_SERVO_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started, enable command published");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

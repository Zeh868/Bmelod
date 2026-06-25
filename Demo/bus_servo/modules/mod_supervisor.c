/**
 * @file mod_supervisor.c
 * @brief bus_servo 伺服监督模块（SRT）：指令发布、状态轮询与故障事件
 *
 * 与 closed_loop_servo 的 mod_supervisor.c 逻辑对等，通讯改用 bm_bus：
 *   - 发布命令：调用 app_bus_servo_publish_command（写 QUEUE）
 *   - 读取状态：调用 app_bus_servo_read_state（读 LATEST）
 *   - 故障注入：调用 app_bus_servo_inject_fault（写 QUEUE，FAULT 位）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿，从 closed_loop_servo 移植 bm_bus 通讯
 *
 */
#include "app_bus_servo.h"
#include "bm_event.h"
#include "bm_log.h"
#include "bm_module.h"

#include <string.h>

#define TAG "bus_sup"

/** 事件订阅 ID（跨事件类型复用同一 ID） */
static bm_event_subscriber_id_t s_sub_id;
/** 命令序号单调递增 */
static uint32_t s_cmd_sequence;

/**
 * @brief 组装并向 QUEUE 发布使能命令
 *
 * @param setpoint_rad_s 速度设定值（rad/s）
 */
static void publish_enable_command(float setpoint_rad_s) {
    bus_servo_command_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = ++s_cmd_sequence;
    cmd.status = BUS_SERVO_CMD_STATUS_ENABLED;
    cmd.velocity_setpoint_rad_s = setpoint_rad_s;
    app_bus_servo_publish_command(&cmd);
    g_bus_servo_metrics.command_publishes++;
}

/**
 * @brief POLL 周期回调：读 LATEST 状态 + 排空两路 SIGNAL 遥测
 *
 * 演示 bm_bus 的两个被动消费模式在同一 SRT 周期里协同工作：
 *   - LATEST：读取轴最新状态（loop_count>0 视为有效样本）
 *   - SIGNAL：导出/监控两个独立消费者各自追赶遥测流，
 *     按本次实际读到的帧数累加各自计数器。
 */
static void poll_state(void) {
    bus_servo_state_t st;
    uint32_t export_frames;
    uint32_t monitor_frames;

    /* LATEST：最新轴状态 */
    memset(&st, 0, sizeof(st));
    app_bus_servo_read_state(&st);
    if (st.loop_count > 0u) {
        g_bus_servo_metrics.state_reads++;
    }

    /* SIGNAL：两个独立消费者周期性追赶遥测流 */
    export_frames = app_bus_servo_drain_telem_export();
    monitor_frames = app_bus_servo_drain_telem_monitor();
    g_bus_servo_metrics.telem_export_reads += export_frames;
    g_bus_servo_metrics.telem_monitor_reads += monitor_frames;
}

/**
 * @brief 事件回调：处理伺服事件
 *
 * @param event 事件指针
 * @param user_data 用户数据（未使用）
 */
static void on_bus_servo_event(const bm_event_t *event, void *user_data) {
    (void)user_data;

    if (event->type == EVENT_BUS_SERVO_ENABLE) {
        publish_enable_command(BUS_SERVO_SETPOINT_RAD_S);
        return;
    }

    if (event->type == EVENT_BUS_SERVO_SETPOINT) {
        float setpoint = BUS_SERVO_SETPOINT_RAD_S;

        if (event->data_len >= sizeof(float)) {
            memcpy(&setpoint, event->data, sizeof(float));
        }
        publish_enable_command(setpoint);
        return;
    }

    if (event->type == EVENT_BUS_SERVO_FAULT) {
        g_bus_servo_metrics.fault_events++;
        app_bus_servo_inject_fault();
        return;
    }

    if (event->type == EVENT_BUS_SERVO_POLL) {
        poll_state();
    }
}

/**
 * @brief 监督模块初始化：注册事件类型并订阅
 *
 * @return BM_OK 成功；错误码表示注册/订阅失败
 */
static int supervisor_init(void) {
    int rc;

    rc = bm_event_register_type(EVENT_BUS_SERVO_ENABLE, "BUS_SERVO_EN");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_BUS_SERVO_SETPOINT, "BUS_SERVO_SP");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_BUS_SERVO_FAULT, "BUS_SERVO_FLT");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_register_type(EVENT_BUS_SERVO_POLL, "BUS_SERVO_POLL");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_BUS_SERVO_ENABLE, on_bus_servo_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_BUS_SERVO_SETPOINT, on_bus_servo_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_BUS_SERVO_FAULT, on_bus_servo_event, NULL, &s_sub_id);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_event_subscribe(EVENT_BUS_SERVO_POLL, on_bus_servo_event, NULL, &s_sub_id);
    return rc;
}

/**
 * @brief 监督模块启动：发布使能事件，触发命令发布
 *
 * @return BM_OK
 */
static int supervisor_start(void) {
    /* 启动后发布使能事件，由本模块回调写入 QUEUE 命令 */
    (void)bm_event_publish_copy(EVENT_BUS_SERVO_ENABLE, 1u, NULL, 0u);
    BM_LOGI(TAG, "supervisor started, enable command published to QUEUE");
    return BM_OK;
}

BM_MODULE_DEFINE(supervisor, 0, supervisor_init, supervisor_start, NULL, NULL);

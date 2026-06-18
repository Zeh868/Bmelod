/**
 * @file app_motor.h
 * @brief motor_foc_sensored 示例：命令/遥测快照与监督层接口
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
#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include "bm/component/motor_foc_sensored.h"

#include <stdint.h>

#define EVENT_MOTOR_ENABLE   1u
#define EVENT_MOTOR_SETPOINT 2u
#define EVENT_MOTOR_FAULT    3u
#define EVENT_MOTOR_POLL     4u

#define MOTOR_SPEED_SETPOINT_RAD_S  5.0f
#define MOTOR_PASS_SPEED_MIN        3.0f

typedef struct {
    uint32_t command_publishes;
    uint32_t telemetry_reads;
    uint32_t fault_events;
} motor_supervisor_metrics_t;

extern bm_motor_foc_sensored_axis_t g_motor_axis;
extern const bm_exec_t g_motor_exec;
extern motor_supervisor_metrics_t g_supervisor_metrics;

void app_motor_publish_command(const bm_motor_foc_cmd_t *command);
void app_motor_read_telemetry(bm_motor_foc_telemetry_t *telemetry);
void app_motor_inject_fault(void);

#endif /* APP_MOTOR_H */

/**
 * @file app_power.h
 * @brief power_buck_loop 示例：命令/遥测快照与监督层共享接口
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
#ifndef APP_POWER_H
#define APP_POWER_H

#include "bm/component/power_control.h"
#include "bm_exec.h"

#include <stdint.h>

#define EVENT_POWER_ENABLE   1u
#define EVENT_POWER_SETPOINT 2u
#define EVENT_POWER_FAULT    3u
#define EVENT_POWER_POLL     4u

#define POWER_SETPOINT_V     1.0f

typedef struct {
    uint32_t command_publishes;
    uint32_t telemetry_reads;
    uint32_t fault_events;
} power_supervisor_metrics_t;

extern bm_power_control_axis_t g_power_axis;
extern const bm_exec_t g_power_exec;
extern power_supervisor_metrics_t g_supervisor_metrics;

void app_power_publish_command(const bm_power_ctrl_cmd_t *command);
void app_power_read_telemetry(bm_power_ctrl_telemetry_t *telemetry);
void app_power_inject_fault(void);

#endif /* APP_POWER_H */

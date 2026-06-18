/**
 * @file app_foc.h
 * @brief foc_current_loop 示例：命令/遥测快照与监督层共享接口
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
#ifndef APP_FOC_H
#define APP_FOC_H

#include "bm_exec.h"

#include <stdint.h>

#define EVENT_FOC_ENABLE   1u
#define EVENT_FOC_SETPOINT 2u
#define EVENT_FOC_FAULT    3u
#define EVENT_FOC_POLL     4u

/** 默认 q 轴电流参考（A） */
#define FOC_IQ_REF_A        1.0f

#define FOC_CMD_STATUS_ENABLED  (1u << 0u)
#define FOC_CMD_STATUS_FAULT    (1u << 1u)

#define FOC_TEL_STATUS_VALID    (1u << 0u)
#define FOC_TEL_STATUS_SAT      (1u << 1u)
#define FOC_TEL_STATUS_FAULT    (1u << 2u)

/** SRT → HRT 电流环命令 */
typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    id_ref_a;
    float    iq_ref_a;
} foc_command_t;

/** HRT → SRT 电流环遥测 */
typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    id_meas_a;
    float    iq_meas_a;
    float    theta_elec_rad;
    float    vd;
    float    vq;
} foc_telemetry_t;

typedef struct {
    uint32_t loop_count;
    float    id_meas_a;
    float    iq_meas_a;
    uint32_t fault_count;
    int      fault_latched;
} foc_axis_state_t;

typedef struct {
    uint32_t command_publishes;
    uint32_t telemetry_reads;
    uint32_t fault_events;
} foc_supervisor_metrics_t;

extern foc_axis_state_t g_foc_axis_state;
extern const bm_exec_t g_foc_axis;
extern foc_supervisor_metrics_t g_supervisor_metrics;

void app_foc_publish_command(const foc_command_t *command);
void app_foc_read_telemetry(foc_telemetry_t *telemetry);
void app_foc_inject_fault(void);

#endif /* APP_FOC_H */

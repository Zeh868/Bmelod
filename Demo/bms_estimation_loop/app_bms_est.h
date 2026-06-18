/**
 * @file app_bms_est.h
 * @brief bms_estimation_loop 示例：遥测快照与监督层共享接口
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
#ifndef APP_BMS_EST_H
#define APP_BMS_EST_H

#include "bm/component/bms_estimation.h"
#include "bm_exec.h"

#include <stdint.h>

#define EVENT_BMS_EST_START  1u
#define EVENT_BMS_EST_POLL   2u

typedef struct {
    uint32_t telemetry_reads;
    float    last_soc;
} bms_est_supervisor_metrics_t;

extern bm_bms_estimation_axis_t g_bms_axis;
extern const bm_exec_t g_bms_exec;
extern bms_est_supervisor_metrics_t g_supervisor_metrics;

void app_bms_est_read_telemetry(bm_bms_est_telemetry_t *telemetry);

#endif /* APP_BMS_EST_H */

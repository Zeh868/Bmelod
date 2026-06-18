/**
 * @file app_stream_frontend.h
 * @brief stream_frontend 示例：块流前端遥测与监督层接口
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始发布
 */
#ifndef APP_STREAM_FRONTEND_H
#define APP_STREAM_FRONTEND_H

#include "bm/component/stream_frontend.h"

#include <stdint.h>

#define EVENT_STREAM_FE_ENABLE  1u
#define EVENT_STREAM_FE_POLL    2u

/** 1 kHz 正弦，32 kHz 采样；幅值 1.0 → RMS ≈ 0.7071 */
#define STREAM_FE_SINE_AMPLITUDE       1.0f
#define STREAM_FE_EXPECTED_RMS         0.70710678f
#define STREAM_FE_RMS_PASS_TOLERANCE   0.02f
#define STREAM_FE_PASS_MIN_BLOCKS      80u

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_drift_ratio;
    uint32_t last_overrun;
    uint32_t last_sequence;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} stream_fe_supervisor_metrics_t;

extern stream_fe_supervisor_metrics_t g_stream_fe_metrics;
extern bm_stream_frontend_axis_t g_stream_fe_axis;

void app_stream_frontend_enable_production(void);

#endif /* APP_STREAM_FRONTEND_H */

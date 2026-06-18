/**
 * @file app_stream.h
 * @brief stream_block_rms 示例：块流生产/消费与监督层接口
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
#ifndef APP_STREAM_H
#define APP_STREAM_H

#include <stdint.h>

#define EVENT_STREAM_ENABLE  1u
#define EVENT_STREAM_POLL    2u

/** 1 kHz 正弦，32 kHz 采样；幅值 1.0 → RMS ≈ 0.7071 */
#define STREAM_SINE_AMPLITUDE       1.0f
#define STREAM_EXPECTED_RMS         0.70710678f
#define STREAM_RMS_PASS_TOLERANCE   0.02f
#define STREAM_PASS_MIN_BLOCKS      80u

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} stream_supervisor_metrics_t;

extern stream_supervisor_metrics_t g_stream_metrics;

/** SRT 侧：响应 EVENT_STREAM_ENABLE，启动块流生产 */
void app_stream_enable_production(void);

#endif /* APP_STREAM_H */

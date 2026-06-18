/**
 * @file app_daq.h
 * @brief daq_frontend 示例：块流 RMS / 波峰因数捕获接口
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
#ifndef APP_DAQ_H
#define APP_DAQ_H

#include <stdint.h>

#define EVENT_DAQ_ENABLE  1u
#define EVENT_DAQ_POLL    2u

/** 1 kHz 正弦幅值 1.0 → RMS ≈ 0.707，波峰因数 ≈ √2 */
#define DAQ_SINE_AMPLITUDE          1.0f
#define DAQ_EXPECTED_RMS            0.70710678f
#define DAQ_EXPECTED_CREST          1.41421356f
#define DAQ_RMS_PASS_TOLERANCE      0.03f
#define DAQ_CREST_PASS_TOLERANCE    0.05f
#define DAQ_PASS_MIN_BLOCKS         80u

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_crest;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} daq_supervisor_metrics_t;

extern daq_supervisor_metrics_t g_daq_metrics;

void app_daq_enable_production(void);

#endif /* APP_DAQ_H */

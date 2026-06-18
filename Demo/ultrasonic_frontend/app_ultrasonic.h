/**
 * @file app_ultrasonic.h
 * @brief ultrasonic_frontend 示例：回波块流 ToF + 同步检波接口
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
#ifndef APP_ULTRASONIC_H
#define APP_ULTRASONIC_H

#include <stdint.h>

#define EVENT_US_ENABLE  1u
#define EVENT_US_POLL    2u

#define US_EXPECTED_TOF_INDEX       20u
/** 至少 2 个载波周期（40 kHz @ 1 Msps ≈ 25 点/周期） */
#define US_BURST_WIDTH              40u
#define US_TOF_PASS_TOLERANCE       4u
#define US_DEMOD_MAG_MIN            0.08f
#define US_PASS_MIN_BLOCKS          60u

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    int32_t  last_tof;
    float    last_demod_mag;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} us_supervisor_metrics_t;

extern us_supervisor_metrics_t g_us_metrics;

void app_us_enable_production(void);

#endif /* APP_ULTRASONIC_H */

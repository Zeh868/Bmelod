/**
 * @file app_vibration.h
 * @brief stream_vibration_diag 示例：块流 + RMS/Goertzel 振动诊断管线
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
#ifndef APP_VIBRATION_H
#define APP_VIBRATION_H

#include <stdint.h>

#define EVENT_VIB_ENABLE  1u
#define EVENT_VIB_POLL    2u

#define VIB_SAMPLES_PER_BLOCK     64u
#define VIB_BLOCK_PERIOD_US       8000u
#define VIB_SAMPLE_RATE_HZ        8000u
#define VIB_BLOCK_DEPTH           4u
#define VIB_TONE_FREQ_HZ          200.0f
#define VIB_INPUT_AMPLITUDE       0.5f
#define VIB_PASS_MIN_BLOCKS       40u
#define VIB_PASS_RMS_MIN          0.30f
#define VIB_PASS_TONE_MIN         0.20f

#define VIB_FMT_RAW       0xB001u
#define VIB_FMT_DIAG      0xB002u

typedef struct {
    float samples[VIB_SAMPLES_PER_BLOCK];
    float block_rms;
    float tone_magnitude;
} vib_accel_block_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_tone_mag;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} vib_supervisor_metrics_t;

extern vib_supervisor_metrics_t g_vib_metrics;

void app_vibration_enable_production(void);

#endif /* APP_VIBRATION_H */

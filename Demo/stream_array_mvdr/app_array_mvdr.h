/**
 * @file app_array_mvdr.h
 * @brief stream_array_mvdr 示例：多通道块流 + MVDR + pipeline LPF
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
#ifndef APP_ARRAY_MVDR_H
#define APP_ARRAY_MVDR_H

#include <stdint.h>

#define EVENT_ARRAY_MVDR_ENABLE  1u
#define EVENT_ARRAY_MVDR_POLL    2u

#define ARRAY_MVDR_NUM_CHANNELS        2u
#define ARRAY_MVDR_SAMPLES_PER_BLOCK     32u
#define ARRAY_MVDR_BLOCK_PERIOD_US       1000u
#define ARRAY_MVDR_SAMPLE_RATE_HZ        8000u
#define ARRAY_MVDR_BLOCK_DEPTH           4u
#define ARRAY_MVDR_SINE_FREQ_HZ          1000.0f
#define ARRAY_MVDR_SINE_AMPLITUDE        0.5f
#define ARRAY_MVDR_CH1_DELAY_SAMPLES     4
#define ARRAY_MVDR_PASS_MIN_BLOCKS       40u
#define ARRAY_MVDR_RMS_PASS_MIN          0.15f

#define ARRAY_MVDR_FMT_RAW               0xB001u
#define ARRAY_MVDR_FMT_MVDR              0xB002u
#define ARRAY_MVDR_FMT_LPF               0xB003u

typedef struct {
    float ch0[ARRAY_MVDR_SAMPLES_PER_BLOCK];
    float ch1[ARRAY_MVDR_SAMPLES_PER_BLOCK];
} array_mvdr_pcm_block_t;

typedef struct {
    float samples[ARRAY_MVDR_SAMPLES_PER_BLOCK];
    float block_rms;
    float block_energy;
} array_mvdr_mono_block_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_energy;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} array_mvdr_supervisor_metrics_t;

extern array_mvdr_supervisor_metrics_t g_array_mvdr_metrics;

void app_array_mvdr_enable_production(void);

#endif /* APP_ARRAY_MVDR_H */

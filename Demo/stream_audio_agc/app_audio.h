/**
 * @file app_audio.h
 * @brief stream_audio_agc 示例：块流 + AGC + 限幅管线
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
#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include <stdint.h>

#define EVENT_AUDIO_ENABLE  1u
#define EVENT_AUDIO_POLL    2u

#define AUDIO_SAMPLES_PER_BLOCK     64u
#define AUDIO_BLOCK_PERIOD_US       4000u
#define AUDIO_SAMPLE_RATE_HZ        16000u
#define AUDIO_BLOCK_DEPTH           4u
#define AUDIO_INPUT_AMPLITUDE       0.1f
#define AUDIO_AGC_TARGET            0.5f
#define AUDIO_PASS_MIN_BLOCKS       50u
#define AUDIO_PASS_LEVEL_MIN        0.35f

#define AUDIO_FMT_RAW                 0xA001u
#define AUDIO_FMT_AGC                 0xA002u
#define AUDIO_FMT_LIMITED             0xA003u

typedef struct {
    float samples[AUDIO_SAMPLES_PER_BLOCK];
    float peak_level;
} audio_pcm_block_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_peak;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} audio_supervisor_metrics_t;

extern audio_supervisor_metrics_t g_audio_metrics;

void app_audio_enable_production(void);

#endif /* APP_AUDIO_H */

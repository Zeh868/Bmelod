/**
 * @file app_fft.h
 * @brief stream_fft 示例：DMA 块流 + RFFT 频谱峰验收接口
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
#ifndef APP_FFT_H
#define APP_FFT_H

#include <stdint.h>

#define EVENT_FFT_ENABLE  1u
#define EVENT_FFT_POLL    2u

/** 1 kHz 正弦 @ 16 kHz 采样，64 点 RFFT → 峰值 bin = 4 */
#define FFT_PASS_MIN_BLOCKS   50u
#define FFT_EXPECTED_PEAK_BIN 4u
#define FFT_PEAK_MAG_MIN      0.25f

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    uint32_t peak_bin;
    float    peak_mag;
    uint32_t telemetry_reads;
    uint32_t enable_events;
} fft_supervisor_metrics_t;

extern fft_supervisor_metrics_t g_fft_metrics;

/** SRT 侧：响应 EVENT_FFT_ENABLE，启动块流生产 */
void app_fft_enable_production(void);

#endif /* APP_FFT_H */

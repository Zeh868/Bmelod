/**
 * @file app_bms_pipeline.h
 * @brief stream_bms_pipeline 示例：块流 + 线性管线 + Pack SOC
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
#ifndef APP_BMS_PIPELINE_H
#define APP_BMS_PIPELINE_H

#include "bms_demo_shared.h"

#include <stdint.h>

#define EVENT_BMS_ENABLE  1u
#define EVENT_BMS_POLL    2u

/** 50 ms 块周期，每块 16 点 → 320 Hz 等效采样 */
#define BMS_SAMPLES_PER_BLOCK       16u
#define BMS_BLOCK_PERIOD_US         50000u
#define BMS_SAMPLE_RATE_HZ          320u
#define BMS_SAMPLE_DT_S             (1.0f / (float)BMS_SAMPLE_RATE_HZ)
#define BMS_BLOCK_DEPTH             4u
#define BMS_BLOCK_BYTES             (BMS_SAMPLES_PER_BLOCK * sizeof(float) + sizeof(float))

#define BMS_PACK_CHARGE_A           BMS_DEMO_CHARGE_CURRENT_A
#define BMS_NOMINAL_CAPACITY_AH     BMS_DEMO_NOMINAL_CAPACITY_AH
#define BMS_SOC_INIT                BMS_DEMO_SOC_INIT
#define BMS_PASS_MIN_BLOCKS         75u
#define BMS_SOC_PASS_MIN            0.515f

#define BMS_FMT_RAW                 0xB001u
#define BMS_FMT_FILTERED            0xB002u
#define BMS_FMT_SOC                 0xB003u

typedef struct {
    float current_a[BMS_SAMPLES_PER_BLOCK];
    float soc;
} bms_pack_block_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_soc;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} bms_supervisor_metrics_t;

extern bms_supervisor_metrics_t g_bms_metrics;

void app_bms_enable_production(void);

#endif /* APP_BMS_PIPELINE_H */

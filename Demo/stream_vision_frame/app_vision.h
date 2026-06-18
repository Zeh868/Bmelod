/**
 * @file app_vision.h
 * @brief stream_vision_frame 示例：帧块流 + 帧差 + 质心跟踪管线
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
#ifndef APP_VISION_H
#define APP_VISION_H

#include <stdint.h>

#define EVENT_VISION_ENABLE  1u
#define EVENT_VISION_POLL    2u

#define VISION_FRAME_WIDTH        32u
#define VISION_FRAME_HEIGHT       24u
#define VISION_FRAME_PIXELS       (VISION_FRAME_WIDTH * VISION_FRAME_HEIGHT)
#define VISION_BLOCK_PERIOD_US    16000u
#define VISION_BLOCK_DEPTH        4u
#define VISION_DIFF_THRESH        16u
#define VISION_PASS_MIN_BLOCKS    30u
#define VISION_PASS_MOTION_MIN    10u
#define VISION_BOX_SIZE           4u

#define VISION_FMT_RAW            0xC001u
#define VISION_FMT_DIFF           0xC002u
#define VISION_FMT_TRACKED        0xC003u

typedef struct {
    uint8_t  pixels[VISION_FRAME_PIXELS];
    uint8_t  diff_mask[VISION_FRAME_PIXELS];
    float    centroid_x;
    float    centroid_y;
    uint8_t  motion_detected;
} vision_frame_block_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    uint32_t motion_frames;
    float    last_cx;
    float    last_cy;
    uint32_t enable_events;
    uint32_t telemetry_reads;
} vision_supervisor_metrics_t;

extern vision_supervisor_metrics_t g_vision_metrics;

void app_vision_enable_production(void);

#endif /* APP_VISION_H */

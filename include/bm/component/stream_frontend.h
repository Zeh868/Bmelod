/**
 * @file stream_frontend.h
 * @brief 块流前端：bm_stream 包装与时钟漂移监督
 *
 * 对 bm_stream/bm_block 提供 on_block_submit/on_block_consume 封装，
 * 集成时钟漂移估计并汇总 overrun/underrun/late 遥测。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#ifndef BM_STREAM_FRONTEND_H
#define BM_STREAM_FRONTEND_H

#include "bm/algorithm/bm_algo_resample.h"
#include "bm/hybrid/bm_block.h"
#include "bm/hybrid/bm_stream.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t expected_block_period_us;
    float    drift_alpha;
} bm_stream_frontend_config_t;

typedef struct {
    uint32_t sequence;
    uint32_t blocks_submitted;
    uint32_t blocks_consumed;
    uint32_t overrun;
    uint32_t underrun;
    uint32_t late;
    float    drift_ratio;
    float    last_interval_us;
} bm_stream_frontend_telemetry_t;

typedef struct {
    bm_algo_clock_drift_state_t drift;
    uint32_t last_submit_us;
    uint32_t last_consume_us;
    int      have_last_submit;
    int      have_last_consume;
    bm_stream_frontend_telemetry_t telemetry;
} bm_stream_frontend_state_t;

typedef struct {
    bm_stream_frontend_config_t config;
    bm_stream_t                *stream;
    bm_stream_frontend_state_t  state;
} bm_stream_frontend_axis_t;

int  bm_stream_frontend_validate_config(
    const bm_stream_frontend_config_t *config);
int  bm_stream_frontend_init(bm_stream_frontend_axis_t *axis,
                             bm_stream_t *stream);
void bm_stream_frontend_reset(bm_stream_frontend_axis_t *axis);
int  bm_stream_frontend_producer_acquire(bm_stream_frontend_axis_t *axis,
                                         bm_block_t **block);
int  bm_stream_frontend_on_block_submit(bm_stream_frontend_axis_t *axis,
                                        bm_block_t *block,
                                        uint32_t valid_bytes,
                                        const bm_timestamp_t *timestamp);
int  bm_stream_frontend_on_block_consume(bm_stream_frontend_axis_t *axis,
                                         bm_block_t **block);
int  bm_stream_frontend_block_release(bm_stream_frontend_axis_t *axis,
                                      bm_block_t *block);

#ifdef __cplusplus
}
#endif

#endif /* BM_STREAM_FRONTEND_H */

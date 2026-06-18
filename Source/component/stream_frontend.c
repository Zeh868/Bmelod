/**
 * @file stream_frontend.c
 * @brief 块流前端实现
 *
 * 包装 bm_stream 生产/消费路径，统计块间隔并对接时钟漂移补偿。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#include "bm/component/stream_frontend.h"
#include "bm/common/bm_types.h"

#include <string.h>

static uint32_t timestamp_to_us(const bm_timestamp_t *ts) {
    if (ts == NULL || ts->rate_hz == 0u) {
        return 0u;
    }
    return (uint32_t)((uint64_t)ts->ticks * 1000000u / (uint64_t)ts->rate_hz);
}

static void sync_stream_stats(bm_stream_frontend_axis_t *axis) {
    const bm_stream_stats_t *stats;

    if (axis == NULL || axis->stream == NULL) {
        return;
    }
    stats = bm_stream_stats(axis->stream);
    if (stats != NULL) {
        axis->state.telemetry.overrun = stats->overrun;
        axis->state.telemetry.underrun = stats->underrun;
        axis->state.telemetry.late = stats->late;
    }
}

static void update_submit_drift(bm_stream_frontend_axis_t *axis,
                                const bm_timestamp_t *timestamp) {
    bm_algo_clock_drift_config_t drift_cfg;
    uint32_t now_us;
    float expected_s;
    float actual_s;

    if (axis == NULL || timestamp == NULL ||
        axis->config.expected_block_period_us == 0u) {
        return;
    }

    now_us = timestamp_to_us(timestamp);
    if (axis->state.have_last_submit) {
        uint32_t delta_us = now_us - axis->state.last_submit_us;

        expected_s = (float)axis->config.expected_block_period_us / 1000000.0f;
        actual_s = (float)delta_us / 1000000.0f;
        drift_cfg.alpha = axis->config.drift_alpha;
        if (drift_cfg.alpha <= 0.0f || drift_cfg.alpha > 1.0f) {
            drift_cfg.alpha = 0.1f;
        }
        bm_algo_clock_drift_feed(&axis->state.drift, &drift_cfg,
                                 expected_s, actual_s);
        axis->state.telemetry.last_interval_us = (float)delta_us;
        axis->state.telemetry.drift_ratio =
            1.0f + axis->state.drift.ratio_error;
    }

    axis->state.last_submit_us = now_us;
    axis->state.have_last_submit = 1;
}

int bm_stream_frontend_validate_config(
    const bm_stream_frontend_config_t *config) {
    if (config == NULL || config->expected_block_period_us == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_stream_frontend_reset(bm_stream_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_clock_drift_reset(&axis->state.drift);
    axis->state.last_submit_us = 0u;
    axis->state.last_consume_us = 0u;
    axis->state.have_last_submit = 0;
    axis->state.have_last_consume = 0;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
    sync_stream_stats(axis);
}

int bm_stream_frontend_init(bm_stream_frontend_axis_t *axis,
                            bm_stream_t *stream) {
    if (axis == NULL || stream == NULL ||
        bm_stream_frontend_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    axis->stream = stream;
    bm_stream_frontend_reset(axis);
    return BM_OK;
}

int bm_stream_frontend_producer_acquire(bm_stream_frontend_axis_t *axis,
                                        bm_block_t **block) {
    if (axis == NULL || axis->stream == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }
    return bm_stream_producer_acquire(axis->stream, block);
}

int bm_stream_frontend_on_block_submit(bm_stream_frontend_axis_t *axis,
                                     bm_block_t *block,
                                     uint32_t valid_bytes,
                                     const bm_timestamp_t *timestamp) {
    int rc;

    if (axis == NULL || axis->stream == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }

    update_submit_drift(axis, timestamp);
    rc = bm_stream_producer_commit(axis->stream, block, valid_bytes,
                                   timestamp);
    if (rc == BM_OK) {
        axis->state.telemetry.blocks_submitted++;
        axis->state.telemetry.sequence++;
    }
    sync_stream_stats(axis);
    return rc;
}

int bm_stream_frontend_on_block_consume(bm_stream_frontend_axis_t *axis,
                                        bm_block_t **block) {
    int rc;
    const bm_timestamp_t *ts;
    uint32_t now_us;
    bm_algo_clock_drift_config_t drift_cfg;
    float expected_s;
    float actual_s;

    if (axis == NULL || axis->stream == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }

    rc = bm_stream_consumer_acquire(axis->stream, block);
    if (rc != BM_OK) {
        sync_stream_stats(axis);
        return rc;
    }

    ts = &(*block)->timestamp;
    now_us = timestamp_to_us(ts);
    if (axis->state.have_last_consume && ts->rate_hz != 0u) {
        uint32_t delta_us = now_us - axis->state.last_consume_us;

        expected_s = (float)axis->config.expected_block_period_us / 1000000.0f;
        actual_s = (float)delta_us / 1000000.0f;
        drift_cfg.alpha = axis->config.drift_alpha;
        if (drift_cfg.alpha <= 0.0f || drift_cfg.alpha > 1.0f) {
            drift_cfg.alpha = 0.1f;
        }
        (void)bm_algo_clock_drift_compensate(&axis->state.drift, actual_s);
        axis->state.telemetry.last_interval_us = (float)delta_us;
        axis->state.telemetry.drift_ratio =
            1.0f + axis->state.drift.ratio_error;
    }

    if (ts->rate_hz != 0u) {
        axis->state.last_consume_us = now_us;
        axis->state.have_last_consume = 1;
    }

    axis->state.telemetry.blocks_consumed++;
    axis->state.telemetry.sequence++;
    sync_stream_stats(axis);
    return BM_OK;
}

int bm_stream_frontend_block_release(bm_stream_frontend_axis_t *axis,
                                     bm_block_t *block) {
    if (axis == NULL || axis->stream == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }
    return bm_stream_consumer_release(axis->stream, block);
}

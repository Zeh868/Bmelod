/**
 * @file stream_frontend.c
 * @brief 块流前端实现
 *
 * 包装 bm_stream 生产/消费路径，统计块间隔并对接时钟漂移补偿。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "bm/component/stream_frontend.h"
#include "bm/common/bm_types.h"

#include <string.h>

/**
 * @brief 将 bm_timestamp_t 转换为微秒数
 */
static uint32_t timestamp_to_us(const bm_timestamp_t *ts) {
    if (ts == NULL || ts->rate_hz == 0u) {
        return 0u;
    }
    return (uint32_t)((uint64_t)ts->ticks * 1000000u / (uint64_t)ts->rate_hz);
}

/**
 * @brief 从底层 bm_stream 同步 overrun/underrun/late 统计到遥测字段
 */
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

/**
 * @brief 根据本次提交时间戳更新时钟漂移估计（生产侧）
 */
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

/**
 * @brief 校验块流前端配置参数
 *
 * @param config 配置结构指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 无效
 */
int bm_stream_frontend_validate_config(
    const bm_stream_frontend_config_t *config) {
    if (config == NULL || config->expected_block_period_us == 0u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 复位块流前端状态（清零漂移估计、时间戳与遥测计数）
 *
 * @param axis 轴实例指针（NULL 时直接返回）
 */
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

/**
 * @brief 初始化块流前端（绑定底层 bm_stream 并复位状态）
 *
 * @param axis   轴实例指针（不可为 NULL；config 须预先填写）
 * @param stream 底层 bm_stream 实例（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
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

/**
 * @brief 向生产侧申请空闲块（直通至 bm_stream_producer_acquire）
 *
 * @param axis  已初始化的轴实例（不可为 NULL）
 * @param block 输出：指向申请到的块指针（不可为 NULL）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
int bm_stream_frontend_producer_acquire(bm_stream_frontend_axis_t *axis,
                                        bm_block_t **block) {
    if (axis == NULL || axis->stream == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }
    return bm_stream_producer_acquire(axis->stream, block);
}

/**
 * @brief 提交已填充块（更新漂移估计后转交底层 commit）
 *
 * @param axis        已初始化的轴实例（不可为 NULL）
 * @param block       待提交的块（不可为 NULL）
 * @param valid_bytes 块内有效字节数
 * @param timestamp   本次提交时间戳（可为 NULL，跳过漂移更新）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
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

/**
 * @brief 从消费侧获取下一可用块（更新消费侧漂移估计）
 *
 * @param axis  已初始化的轴实例（不可为 NULL）
 * @param block 输出：指向取得的块指针（不可为 NULL）
 * @return BM_OK 成功；其他值来自底层 bm_stream（如缓冲为空）
 */
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

/**
 * @brief 释放消费侧持有的块（归还至底层 bm_stream）
 *
 * @param axis  已初始化的轴实例（不可为 NULL）
 * @param block 待释放的块（不可为 NULL）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
int bm_stream_frontend_block_release(bm_stream_frontend_axis_t *axis,
                                     bm_block_t *block) {
    if (axis == NULL || axis->stream == NULL || block == NULL) {
        return BM_ERR_INVALID;
    }
    return bm_stream_consumer_release(axis->stream, block);
}

/**
 * @file stream_frontend.h
 * @brief 块流前端：bm_stream 包装与时钟漂移监督
 *
 * 对 bm_stream/bm_block 提供 on_block_submit/on_block_consume 封装，
 * 集成时钟漂移估计并汇总 overrun/underrun/late 遥测。
 *
 * @maturity E1
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

/**
 * @brief 校验块流前端配置参数
 * @param config 配置结构指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 无效
 */
int  bm_stream_frontend_validate_config(
    const bm_stream_frontend_config_t *config);

/**
 * @brief 初始化块流前端（绑定底层 bm_stream 并复位状态）
 * @param axis   轴实例指针（不可为 NULL；config 须预先填写）
 * @param stream 底层 bm_stream 实例（不可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int  bm_stream_frontend_init(bm_stream_frontend_axis_t *axis,
                             bm_stream_t *stream);

/**
 * @brief 复位块流前端状态（清零漂移估计、时间戳与遥测计数）
 * @param axis 轴实例指针（NULL 时直接返回）
 */
void bm_stream_frontend_reset(bm_stream_frontend_axis_t *axis);

/**
 * @brief 向生产侧申请空闲块
 * @param axis  已初始化的轴实例（不可为 NULL）
 * @param block 输出：指向申请到的块指针（不可为 NULL）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
int  bm_stream_frontend_producer_acquire(bm_stream_frontend_axis_t *axis,
                                         bm_block_t **block);

/**
 * @brief 提交已填充块（更新漂移估计后转交底层 commit）
 * @param axis        已初始化的轴实例（不可为 NULL）
 * @param block       待提交的块（不可为 NULL）
 * @param valid_bytes 块内有效字节数
 * @param timestamp   本次提交时间戳（可为 NULL，跳过漂移更新）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
int  bm_stream_frontend_on_block_submit(bm_stream_frontend_axis_t *axis,
                                        bm_block_t *block,
                                        uint32_t valid_bytes,
                                        const bm_timestamp_t *timestamp);

/**
 * @brief 从消费侧获取下一可用块（更新消费侧漂移估计）
 * @param axis  已初始化的轴实例（不可为 NULL）
 * @param block 输出：指向取得的块指针（不可为 NULL）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
int  bm_stream_frontend_on_block_consume(bm_stream_frontend_axis_t *axis,
                                         bm_block_t **block);

/**
 * @brief 释放消费侧持有的块（归还至底层 bm_stream）
 * @param axis  已初始化的轴实例（不可为 NULL）
 * @param block 待释放的块（不可为 NULL）
 * @return BM_OK 成功；其他值来自底层 bm_stream
 */
int  bm_stream_frontend_block_release(bm_stream_frontend_axis_t *axis,
                                      bm_block_t *block);

#ifdef __cplusplus
}
#endif

#endif /* BM_STREAM_FRONTEND_H */

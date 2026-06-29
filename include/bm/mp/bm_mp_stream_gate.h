/**
 * @file bm_mp_stream_gate.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 为 hard/block realtime profile 提供 stream 队列深度、服务率与
 * `C_stream_scan/commit/cache` 静态校验；未闭合时 profile build 须 fail-closed。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#ifndef BM_MP_STREAM_GATE_H
#define BM_MP_STREAM_GATE_H

#include "bm/mp/bm_mp_schedule.h"
#include "bm/common/bm_types.h"

#ifndef BM_CONFIG_MP_STREAM_SCAN_US_PER_BLOCK
#define BM_CONFIG_MP_STREAM_SCAN_US_PER_BLOCK  2u
#endif

#ifndef BM_CONFIG_MP_STREAM_COMMIT_US_DEFAULT
#define BM_CONFIG_MP_STREAM_COMMIT_US_DEFAULT  5u
#endif

#ifndef BM_CONFIG_MP_STREAM_CACHE_MAINT_US_DEFAULT
#define BM_CONFIG_MP_STREAM_CACHE_MAINT_US_DEFAULT  10u
#endif

/** 单条 stream 的静态 gate 输入（离线测量或仿真标定） */
typedef struct {
    uint32_t block_count;
    uint32_t block_period_us;
    uint32_t processing_wcet_us;
    uint32_t block_deadline_us;
    uint32_t response_window_us;
    uint32_t burst_blocks;
    uint32_t scan_us_per_block;
    uint32_t commit_us;
    uint32_t cache_maint_us;
} bm_mp_stream_gate_params_t;

/** gate 推导摘要（供 schedule 与 profile build 使用） */
typedef struct {
    uint32_t min_queue_depth;
    uint32_t derived_scan_us;
    uint32_t max_queue_delay_us;
    int      service_sustainable;
} bm_mp_stream_gate_report_t;

/**
 * @brief 由 block_count 推导最坏情况线性扫描成本
 *
 * @param block_count 流式块槽数量上界
 * @param scan_us_per_block 单块扫描预算（微秒）
 * @return `block_count * scan_us_per_block`；参数非法时返回 0
 */
uint32_t bm_mp_stream_derive_scan_us(uint32_t block_count,
                                     uint32_t scan_us_per_block);

/**
 * @brief 由到达/服务曲线推导最小队列深度
 *
 * @param params gate 输入（不可为 NULL）
 * @param report_out 推导摘要（可为 NULL）
 * @return 最小深度；0 表示持续服务率不足
 */
uint32_t bm_mp_stream_derive_min_depth(
    const bm_mp_stream_gate_params_t *params,
    bm_mp_stream_gate_report_t *report_out);

/**
 * @brief 校验 stream gate 闭包（深度、服务率、deadline）
 *
 * @param params gate 输入
 * @param report_out 推导摘要（可为 NULL）
 * @return BM_OK 通过；BM_ERR_INVALID / BM_ERR_OVERFLOW 为 profile 拒绝
 */
int bm_mp_stream_validate_gate(const bm_mp_stream_gate_params_t *params,
                               bm_mp_stream_gate_report_t *report_out);

/**
 * @brief 将 gate 推导结果写入 schedule 槽的 stream 开销字段
 *
 * 仅处理带 `BM_MP_SCHEDULE_FLAG_STREAM` 的槽；缺省 commit/cache 使用配置宏。
 *
 * @param slot 可写 schedule 槽
 * @param params gate 输入
 * @return BM_OK 成功
 */
int bm_mp_schedule_apply_stream_gate(bm_mp_schedule_slot_t *slot,
                                     const bm_mp_stream_gate_params_t *params);

/**
 * @brief 注册 stream 槽：先 apply gate 再 `bm_mp_schedule_register`
 *
 * @param slot schedule 槽描述
 * @param gate stream gate 输入
 * @return BM_OK 成功
 */
int bm_mp_schedule_register_stream(
    const bm_mp_schedule_slot_t *slot,
    const bm_mp_stream_gate_params_t *gate);

#endif /* BM_MP_STREAM_GATE_H */

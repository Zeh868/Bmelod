/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_health.h
 * @brief 运行时健康快照薄聚合层
 *
 * 一次调用聚合 event/hrt/ticker 全局计数，并提供把按实例的 bus
 * 统计折入快照的接口。零动态分配、非阻塞、WCET 可静态分析。
 *
 * @note 分层：hybrid 层（可依赖 core 的 event/bus 与 hybrid 的 hrt/ticker）。
 *       core 层不引用 hybrid，依赖方向保持 hybrid→core。
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       0.1            zeh            初稿：健康快照聚合 API
 *
 */
#ifndef BM_HEALTH_H
#define BM_HEALTH_H

#include "bm/common/bm_types.h"
#include "bm/core/bm_bus.h"

#include <stdint.h>

/**
 * @brief 运行时健康快照结构体
 *
 * 所有字段均为累计值（自上次子系统 reset 或启动以来）。
 * 通过 bm_health_snapshot() 填充全局/单例子系统字段，
 * 通过 bm_health_snapshot_add_bus() 折入按实例 bus 统计。
 */
typedef struct {
    uint32_t event_dropped;              /**< event 总线队列满丢弃次数 */
    uint32_t event_dispatch_skipped;     /**< 分发阶段跳过次数（类型无效或队列损坏） */
    uint32_t event_reentrancy_rejected;  /**< 重入或冻结后被拒绝的 API 调用次数 */
    uint32_t hrt_deadline_missed_total;  /**< 所有 HRT 槽 deadline 错过次数之和 */
    uint32_t ticker_dropped_total;       /**< 所有 ticker 槽丢弃次数之和 */
    uint32_t bus_overflow_total;         /**< 所有折入 bus 的 overflow 累计（由 bm_bus_stats 提供） */
    uint32_t bus_write_total;            /**< 所有折入 bus 的 write 累计（由 bm_bus_stats 提供） */
} bm_health_snapshot_t;

/**
 * @brief 采集全局/单例子系统健康计数到快照
 *
 * 清零 *out 后依次填入：
 *   - event_dropped / event_dispatch_skipped / event_reentrancy_rejected（event 三项）
 *   - hrt_deadline_missed_total（全部 HRT 槽之和）
 *   - ticker_dropped_total（全部 ticker 槽之和）
 *
 * bus_* 字段置 0，由调用方逐 bus 调用 bm_health_snapshot_add_bus() 折入。
 *
 * @note 纯读聚合、无动态分配、非阻塞；对 ticker/hrt 槽的求和均为定长
 *       循环（上界 = slot_count），WCET 可静态分析。
 *
 * @param out 输出快照指针；为 NULL 时直接返回，无副作用
 */
void bm_health_snapshot(bm_health_snapshot_t *out);

/**
 * @brief 把单个 bus 实例的统计折入健康快照
 *
 * 读取 bus 的 write_count 与 overflow_count（通过 bm_bus_stats），
 * 以饱和加法累加到 acc->bus_write_total 与 acc->bus_overflow_total。
 * 调用方对关心的每个 bus 逐个折入，保持零分配与确定性。
 *
 * @note bus_overflow_total 反映 bm_bus_stats 返回的 overflow_count；
 *       当前 bus 实现该字段为写侧计数，BLOCK 模式统计由后端持有（见 bm_bus.h）。
 *
 * @param acc 目标快照指针
 * @param bus bus 句柄指针（只读）
 * @return BM_OK 成功；BM_ERR_INVALID acc 或 bus 为 NULL
 */
int bm_health_snapshot_add_bus(bm_health_snapshot_t *acc, const bm_bus_t *bus);

#endif /* BM_HEALTH_H */

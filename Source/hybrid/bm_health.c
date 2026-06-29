/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_health.c
 * @brief 运行时健康快照薄聚合层实现
 *
 * 纯读聚合：一次调用读取各子系统已有 getter，无侧效应、无动态分配、非阻塞。
 * bus 字段由调用方逐实例折入（控制反转，避免全局 bus 注册表）。
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       0.1            zeh            初稿：健康快照聚合实现
 *
 */
#include "bm/hybrid/bm_health.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_bus.h"
#include "bm_hrt.h"
#include "bm_ticker.h"
#include "bm_safety.h"

#include <string.h>

/**
 * @brief 采集全局/单例子系统健康计数到快照
 *
 * @param out 输出快照指针；为 NULL 时直接返回
 */
void bm_health_snapshot(bm_health_snapshot_t *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    out->event_dropped             = bm_event_get_dropped_count();
    out->event_dispatch_skipped    = bm_event_get_dispatch_skipped_count();
    out->event_reentrancy_rejected = bm_event_get_reentrancy_rejected_count();
    out->hrt_deadline_missed_total = bm_hrt_get_deadline_missed_total();
    out->ticker_dropped_total      = bm_ticker_get_dropped_total();
    /* bus_overflow_total 与 bus_write_total 置 0，由调用方逐 bus 折入 */
}

/**
 * @brief 把单个 bus 实例的统计折入健康快照
 *
 * @param acc 目标快照指针
 * @param bus bus 句柄指针（只读）
 * @return BM_OK 成功；BM_ERR_INVALID acc 或 bus 为 NULL
 */
int bm_health_snapshot_add_bus(bm_health_snapshot_t *acc, const bm_bus_t *bus) {
    bm_bus_stats_t stats;
    int rc;

    if (!acc || !bus) {
        return BM_ERR_INVALID;
    }
    rc = bm_bus_stats(bus, &stats);
    if (rc != BM_OK) {
        return rc;
    }
    acc->bus_write_total    = bm_u32_saturating_add(acc->bus_write_total,
                                                    stats.write_count);
    acc->bus_overflow_total = bm_u32_saturating_add(acc->bus_overflow_total,
                                                    stats.overflow_count);
    return BM_OK;
}

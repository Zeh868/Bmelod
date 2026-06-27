/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_ticker.h
 * @brief 毫秒级周期事件发布器
 *
 * 按固定周期向事件总线发布 ticker 事件，适用于慢速后台任务触发。
 *
 * @core_affinity 本核（按 CPU 分域）
 * 每个 CPU 独立 ticker 槽表，bm_ticker_init/poll 仅操作调用者所在 CPU 的定时器槽。
 * 事件通过 bm_event 发布到本核事件总线；不同 CPU 的订阅由 bm_event 自动转发。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-26       1.1            zeh            新增 bm_ticker_get_dropped_total（对称 hrt total getter）
 *
 */
#ifndef BM_TICKER_H
#define BM_TICKER_H

#include "bm/core/bm_event.h"
#include "bm/common/bm_types.h"

/** Ticker 槽位配置 */
typedef struct {
    uint32_t period_ms;
    bm_event_type_t event_type;
    bm_event_priority_t priority;
    const char *name;
} bm_ticker_slot_t;

/**
 * @brief 初始化 ticker 并注册 slot 表
 *
 * 仅限主上下文调用；不可与 `bm_ticker_poll` 并发。
 *
 * @param slots slot 描述数组
 * @param slot_count slot 数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_ticker_init(const bm_ticker_slot_t *slots, uint32_t slot_count);

/**
 * @brief 轮询 ticker 并发布到期事件
 *
 * 非可重入，仅限主循环调用。
 *
 * @return 本次发布的事件数；负值为未初始化或事件发布错误
 */
int bm_ticker_poll(void);

/**
 * @brief 查询指定 slot 因队列满而丢弃的事件计数
 *
 * @param slot_index slot 索引
 * @return 丢弃事件计数
 */
uint32_t bm_ticker_get_dropped(uint32_t slot_index);

/**
 * @brief 重置 ticker 内部状态
 *
 * 仅限主上下文调用；不可与 `bm_ticker_poll` 并发。
 */
void bm_ticker_reset(void);

/**
 * @brief 查询 ticker 是否已初始化（只读）
 *
 * @return 1 已初始化；0 未初始化
 */
int bm_ticker_is_initialized(void);

/**
 * @brief 查询所有 slot 累计因队列满而丢弃的事件总计数
 *
 * 对当前核所有已注册 slot 的 dropped 计数求和（饱和加法），
 * 与 bm_hrt_get_deadline_missed_total() 语义对称。
 * 定长循环（上界 = slot_count），WCET 可静态分析。
 *
 * @return 各槽 `bm_ticker_get_dropped` 之和；未初始化或 CPU 无效时返回 0
 */
uint32_t bm_ticker_get_dropped_total(void);

#endif /* BM_TICKER_H */

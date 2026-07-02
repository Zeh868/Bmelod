/**
 * @file bm_mp_schedule.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
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
#ifndef BM_MP_SCHEDULE_H
#define BM_MP_SCHEDULE_H

#include "bm/mp/bm_mp_types.h"
#include "bm/common/bm_types.h"

#ifndef BM_CONFIG_SCHEDULE_MAX_SLOTS
#define BM_CONFIG_SCHEDULE_MAX_SLOTS  32u
#endif

#ifndef BM_CONFIG_MP_SCHEDULE_MAX_SLOTS
#define BM_CONFIG_MP_SCHEDULE_MAX_SLOTS  BM_CONFIG_SCHEDULE_MAX_SLOTS
#endif

#ifndef BM_CONFIG_MP_RTA_MAX_ITERATIONS
#define BM_CONFIG_MP_RTA_MAX_ITERATIONS  BM_CONFIG_RTA_MAX_ITERATIONS
#endif

#ifndef BM_CONFIG_MP_BOOTSTRAP_CPU
#define BM_CONFIG_MP_BOOTSTRAP_CPU  BM_CONFIG_BOOTSTRAP_CPU
#endif

#ifndef BM_CONFIG_MP_LOG_DRAIN_WCET_PER_MSG_US
#define BM_CONFIG_MP_LOG_DRAIN_WCET_PER_MSG_US  BM_CONFIG_LOG_DRAIN_WCET_PER_MSG_US
#endif

#ifndef BM_CONFIG_MP_RELAY_DRAIN_WCET_PER_SLOT_US
#define BM_CONFIG_MP_RELAY_DRAIN_WCET_PER_SLOT_US  BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US
#endif

#ifndef BM_CONFIG_MP_STREAM_DRAIN_WCET_PER_BLOCK_US
#define BM_CONFIG_MP_STREAM_DRAIN_WCET_PER_BLOCK_US  BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US
#endif

#ifndef BM_CONFIG_MP_MAIN_LOOP_DEADLINE_US
#define BM_CONFIG_MP_MAIN_LOOP_DEADLINE_US  BM_CONFIG_MAIN_LOOP_DEADLINE_US
#endif

#ifndef BM_CONFIG_MP_MAIN_LOOP_PERIOD_US
#define BM_CONFIG_MP_MAIN_LOOP_PERIOD_US  BM_CONFIG_MAIN_LOOP_PERIOD_US
#endif

#ifndef BM_CONFIG_MP_MAIN_LOOP_FIXED_OVERHEAD_US
#define BM_CONFIG_MP_MAIN_LOOP_FIXED_OVERHEAD_US  \
    BM_CONFIG_MAIN_LOOP_FIXED_OVERHEAD_US
#endif

#ifndef BM_CONFIG_MP_STREAM_ACCOUNT_WCET_PER_BLOCK_US
#define BM_CONFIG_MP_STREAM_ACCOUNT_WCET_PER_BLOCK_US  1u
#endif

#ifndef BM_CONFIG_MP_LOG_DRAIN_BUDGET
#define BM_CONFIG_MP_LOG_DRAIN_BUDGET  BM_CONFIG_LOG_DRAIN_BUDGET
#endif

#if defined(BM_CONFIG_MP_SCHEDULE_MAX_SLOTS) && \
    !defined(BM_CONFIG_SCHEDULE_MAX_SLOTS)
#define BM_CONFIG_SCHEDULE_MAX_SLOTS  BM_CONFIG_MP_SCHEDULE_MAX_SLOTS
#endif

#if defined(BM_CONFIG_MP_RTA_MAX_ITERATIONS) && \
    !defined(BM_CONFIG_RTA_MAX_ITERATIONS)
#define BM_CONFIG_RTA_MAX_ITERATIONS  BM_CONFIG_MP_RTA_MAX_ITERATIONS
#endif

#if defined(BM_CONFIG_MP_BOOTSTRAP_CPU) && !defined(BM_CONFIG_BOOTSTRAP_CPU)
#define BM_CONFIG_BOOTSTRAP_CPU  BM_CONFIG_MP_BOOTSTRAP_CPU
#endif

#if defined(BM_CONFIG_MP_LOG_DRAIN_WCET_PER_MSG_US) && \
    !defined(BM_CONFIG_LOG_DRAIN_WCET_PER_MSG_US)
#define BM_CONFIG_LOG_DRAIN_WCET_PER_MSG_US  \
    BM_CONFIG_MP_LOG_DRAIN_WCET_PER_MSG_US
#endif

#if defined(BM_CONFIG_MP_RELAY_DRAIN_WCET_PER_SLOT_US) && \
    !defined(BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US)
#define BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US  \
    BM_CONFIG_MP_RELAY_DRAIN_WCET_PER_SLOT_US
#endif

#if defined(BM_CONFIG_MP_STREAM_DRAIN_WCET_PER_BLOCK_US) && \
    !defined(BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US)
#define BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US  \
    BM_CONFIG_MP_STREAM_DRAIN_WCET_PER_BLOCK_US
#endif

#if defined(BM_CONFIG_MP_MAIN_LOOP_DEADLINE_US) && \
    !defined(BM_CONFIG_MAIN_LOOP_DEADLINE_US)
#define BM_CONFIG_MAIN_LOOP_DEADLINE_US  BM_CONFIG_MP_MAIN_LOOP_DEADLINE_US
#endif

#if defined(BM_CONFIG_MP_MAIN_LOOP_PERIOD_US) && \
    !defined(BM_CONFIG_MAIN_LOOP_PERIOD_US)
#define BM_CONFIG_MAIN_LOOP_PERIOD_US  BM_CONFIG_MP_MAIN_LOOP_PERIOD_US
#endif

#if defined(BM_CONFIG_MP_MAIN_LOOP_FIXED_OVERHEAD_US) && \
    !defined(BM_CONFIG_MAIN_LOOP_FIXED_OVERHEAD_US)
#define BM_CONFIG_MAIN_LOOP_FIXED_OVERHEAD_US  \
    BM_CONFIG_MP_MAIN_LOOP_FIXED_OVERHEAD_US
#endif

#if BM_CONFIG_MP_STREAM_ACCOUNT_WCET_PER_BLOCK_US == 0u
#error "BM_CONFIG_MP_STREAM_ACCOUNT_WCET_PER_BLOCK_US must be non-zero"
#endif

#if defined(BM_CONFIG_MP_LOG_DRAIN_BUDGET) && \
    !defined(BM_CONFIG_LOG_DRAIN_BUDGET)
#define BM_CONFIG_LOG_DRAIN_BUDGET  BM_CONFIG_MP_LOG_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_MP_STREAM_DRAIN_BUDGET
#define BM_CONFIG_MP_STREAM_DRAIN_BUDGET  BM_CONFIG_STREAM_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET
#define BM_CONFIG_MP_IPC_PER_SOURCE_BUDGET  BM_CONFIG_IPC_PER_SOURCE_BUDGET
#endif

#ifndef BM_CONFIG_MP_IPC_DRAIN_WCET_PER_MSG_US
#define BM_CONFIG_MP_IPC_DRAIN_WCET_PER_MSG_US  BM_CONFIG_IPC_DRAIN_WCET_PER_MSG_US
#endif

/**
 * @brief 主循环 event process 每条消息的 WCET 预算（微秒）
 *
 * @details 语义独立于 IPC 拷贝 WCET（BM_CONFIG_MP_IPC_DRAIN_WCET_PER_MSG_US）：
 * 事件分发是"队列取出 + 回调派发"，与跨核 IPC payload 拷贝并非同一工作量，
 * 复用后者常量属语义错位（P2-3）。默认桥接到 IPC 每消息 WCET 以保持既有数值
 * 行为不变；配置可独立覆盖本旋钮，使预算更贴近事件派发的实测 WCET。
 */
#ifndef BM_CONFIG_MP_EVENT_PROCESS_WCET_PER_MSG_US
#define BM_CONFIG_MP_EVENT_PROCESS_WCET_PER_MSG_US \
    BM_CONFIG_MP_IPC_DRAIN_WCET_PER_MSG_US
#endif

#ifndef BM_CONFIG_MP_RELAY_DRAIN_BUDGET
#define BM_CONFIG_MP_RELAY_DRAIN_BUDGET  BM_CONFIG_RELAY_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_MP_EVENT_PROCESS_BUDGET
#define BM_CONFIG_MP_EVENT_PROCESS_BUDGET  BM_CONFIG_EVENT_PROCESS_BUDGET
#endif

#define BM_MP_SCHEDULE_FLAG_STREAM  (1u << 0)
#define BM_MP_SCHEDULE_FLAG_RELAY   (1u << 1)

/** 离线测量或仿真得到的任务预算条目 */
typedef struct {
    const char *name;
    uint8_t     owner_cpu;
    uint32_t    wcet_us;
    uint32_t    period_us;
    uint32_t    deadline_us;
    uint32_t    blocking_us;
    uint32_t    bus_interference_us;
    uint32_t    flags;
    uint32_t    stream_scan_us;
    uint32_t    stream_commit_us;
    uint32_t    relay_copy_us;
    uint32_t    cache_maint_us;
} bm_mp_schedule_slot_t;

/** 单核校验摘要 */
typedef struct {
    uint8_t   cpu;
    uint32_t  slot_count;
    uint32_t  utilization_ppm;
    uint32_t  worst_response_us;
    int       passed;
} bm_mp_schedule_cpu_report_t;

int bm_mp_schedule_register(const bm_mp_schedule_slot_t *slot);
void bm_mp_schedule_reset(void);

/**
 * @brief 获取当前 schedule 槽位数量快照。
 *
 * 该标记可用于失败回滚；与 @ref bm_mp_schedule_restore 配对使用。
 *
 * @return 当前槽位数量快照。
 */
uint32_t bm_mp_schedule_mark(void);

/**
 * @brief 回滚 schedule 到指定快照。
 *
 * @param mark 由 @ref bm_mp_schedule_mark 返回的槽位数量快照。
 */
void bm_mp_schedule_restore(uint32_t mark);

/**
 * @brief 校验指定 CPU 的响应时间上界
 *
 * @param cpu 逻辑 CPU
 * @param report_out 输出摘要（可为 NULL）
 * @return BM_OK 通过；负值为预算失败
 */
int bm_mp_partition_validate_schedule(uint8_t cpu,
                                    bm_mp_schedule_cpu_report_t *report_out);

/**
 * @brief 打印全核 schedule 报告到日志
 */
void bm_mp_schedule_print_report(void);

/**
 * @brief 将主循环 drain/process 固定预算注册为 schedule 槽（profile build 前调用）
 */
int bm_mp_schedule_register_main_loop_overhead(uint8_t cpu);

#endif /* BM_MP_SCHEDULE_H */

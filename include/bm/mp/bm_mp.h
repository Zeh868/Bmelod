/**
 * @file bm_mp.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 单 ELF 多核对称运行时入口：`bm_mp_cpu_main()` 主循环顺序为
 * stream drain → IPC drain → ticker → event process。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            stream gate；hard RT deadline safe-stop
 *
 */
#ifndef BM_MP_H
#define BM_MP_H

#include "bm/mp/bm_mp_types.h"
#include "bm/mp/bm_mp_boot.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_ipc.h"
#include "bm/mp/bm_mp_schedule.h"
#include "bm/mp/bm_mp_wdg.h"
#include "bm/mp/bm_mp_cpu.h"
#include "bm/mp/bm_mp_stream_gate.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm/mp/bm_mp_resource_topology.h"
#include "bm/hybrid/bm_exec.h"
#include "bm/hybrid/bm_stream_relay.h"

#ifndef BM_CONFIG_EVENT_PROCESS_BUDGET
#define BM_CONFIG_EVENT_PROCESS_BUDGET  16u
#endif

#if defined(BM_CONFIG_MP_EVENT_PROCESS_BUDGET) && \
    !defined(BM_CONFIG_EVENT_PROCESS_BUDGET)
#define BM_CONFIG_EVENT_PROCESS_BUDGET  BM_CONFIG_MP_EVENT_PROCESS_BUDGET
#endif

#ifndef BM_CONFIG_STREAM_DRAIN_BUDGET
#define BM_CONFIG_STREAM_DRAIN_BUDGET  4u
#endif

#if defined(BM_CONFIG_MP_STREAM_DRAIN_BUDGET) && \
    !defined(BM_CONFIG_STREAM_DRAIN_BUDGET)
#define BM_CONFIG_STREAM_DRAIN_BUDGET  BM_CONFIG_MP_STREAM_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_RELAY_DRAIN_BUDGET
#define BM_CONFIG_RELAY_DRAIN_BUDGET   4u
#endif

#if defined(BM_CONFIG_MP_RELAY_DRAIN_BUDGET) && \
    !defined(BM_CONFIG_RELAY_DRAIN_BUDGET)
#define BM_CONFIG_RELAY_DRAIN_BUDGET  BM_CONFIG_MP_RELAY_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_LOG_MP_RING
#define BM_CONFIG_LOG_MP_RING  BM_CONFIG_LOG_RING
#endif

#if defined(BM_CONFIG_LOG_MP_RING) && !defined(BM_CONFIG_LOG_RING)
#define BM_CONFIG_LOG_RING  BM_CONFIG_LOG_MP_RING
#endif

#ifndef BM_CONFIG_MP_EVENT_PROCESS_BUDGET
#define BM_CONFIG_MP_EVENT_PROCESS_BUDGET  BM_CONFIG_EVENT_PROCESS_BUDGET
#endif

#ifndef BM_CONFIG_MP_STREAM_DRAIN_BUDGET
#define BM_CONFIG_MP_STREAM_DRAIN_BUDGET  BM_CONFIG_STREAM_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_MP_RELAY_DRAIN_BUDGET
#define BM_CONFIG_MP_RELAY_DRAIN_BUDGET  BM_CONFIG_RELAY_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_MP_LOG_DRAIN_BUDGET
#define BM_CONFIG_MP_LOG_DRAIN_BUDGET  BM_CONFIG_LOG_DRAIN_BUDGET
#endif

#ifndef BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS
#define BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS  BM_CONFIG_MAIN_LOOP_MAX_SPINS
#endif

#if defined(BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS) && \
    !defined(BM_CONFIG_MAIN_LOOP_MAX_SPINS)
#define BM_CONFIG_MAIN_LOOP_MAX_SPINS  BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS
#endif

#ifndef BM_CONFIG_MP_MAIN_LOOP_PERIOD_US
#define BM_CONFIG_MP_MAIN_LOOP_PERIOD_US  BM_CONFIG_MAIN_LOOP_PERIOD_US
#endif

#if defined(BM_CONFIG_MP_MAIN_LOOP_PERIOD_US) && \
    !defined(BM_CONFIG_MAIN_LOOP_PERIOD_US)
#define BM_CONFIG_MAIN_LOOP_PERIOD_US  BM_CONFIG_MP_MAIN_LOOP_PERIOD_US
#endif

/**
 * @brief 初始化多核剖面（Bootstrap 调用）
 *
 * @param cpu_count 逻辑 CPU 数（须与 BM_CONFIG_CPU_COUNT 一致）
 * @return BM_OK 成功
 */
int bm_mp_init(uint32_t cpu_count);

typedef int (*bm_mp_cpu_init_fn_t)(void);
typedef void (*bm_mp_cpu_iter_fn_t)(void);

/**
 * @brief 注册某核 attach 后、INIT barrier 前的初始化钩子
 */
void bm_mp_set_cpu_init_hook(uint8_t cpu, bm_mp_cpu_init_fn_t fn);

/**
 * @brief 注册主循环每轮迭代钩子（relay drain 等）
 */
void bm_mp_set_cpu_iter_hook(uint8_t cpu, bm_mp_cpu_iter_fn_t fn);

/**
 * @brief 设置从核有限主循环轮次（Demo 用；0 = 仅由 stop 信号结束）
 */
void bm_mp_set_demo_max_loops(uint32_t max_loops);

/**
 * @brief 通知从核结束 Demo 主循环（与 `bm_mp_join_secondary_cpus` 配合）
 */
void bm_mp_signal_demo_stop(void);

/**
 * @brief 等待所有从核线程退出（native_sim）
 *
 * @return BM_OK 成功
 */
int bm_mp_join_secondary_cpus(void);

/**
 * @brief 各核对称主循环（attach 完成后进入）
 */
void bm_mp_cpu_main(void);

/**
 * @brief 主循环单轮迭代（drain / IPC / ticker / event / wdg feed）
 */
void bm_mp_cpu_main_iteration(void);

/**
 * @brief 有限轮次主循环（Demo / 测试用）
 *
 * @param max_loops 最大迭代次数；0 表示无限
 */
void bm_mp_cpu_main_limited(uint32_t max_loops);

/**
 * @brief 从核线程入口：attach → barrier → 有限主循环
 */
void bm_mp_cpu_secondary_entry(void);

/**
 * @brief 主循环空闲等待（WFI 或仿真 yield）
 */
void bm_mp_idle_until_interrupt(void);

/**
 * @brief 强制主循环最小周期间隔（确定性流式时间约束）
 *
 * 自 @p iteration_start_ticks 起忙等待直到 BM_CONFIG_MP_MAIN_LOOP_PERIOD_US
 * 已过。调用者应在每轮迭代起点的 bm_hal_timer_get_ticks() 快照传入。
 * 设为 0 则不强制。
 *
 * @param iteration_start_ticks 本轮迭代开始时的 tick 快照
 */
void bm_mp_enforce_main_loop_period(uint32_t iteration_start_ticks);

/**
 * @brief 纯函数：判断自 @p start_ticks 起是否已过 @p period_us
 *
 * 抽出为可独立测试的纯判定，使"确定性流式核心时序约束"可被单元测试覆盖，
 * 而不依赖真机自旋。回绕安全（now - start 在 uint32_t 模算下对单周期正确）。
 *
 * @param start_ticks  本轮迭代起点 tick 快照
 * @param now_ticks    当前 tick
 * @param freq         定时器频率（Hz）；0 视为已满足（无可用时基）
 * @param period_us    目标最小周期（微秒）；0 视为已满足（不强制）
 * @return 1 表示已达周期；0 表示尚未达到
 */
int bm_mp_main_loop_period_elapsed(uint32_t start_ticks, uint32_t now_ticks,
                                   uint32_t freq, uint32_t period_us);

/**
 * @brief 主循环周期等待超限累计次数（饱和；多核共享，原子计数）
 *
 * 非 0 表示某轮迭代在自旋上限内未能强制最小周期，确定性速率约束本轮失效。
 *
 * @return 累计超限次数
 */
uint32_t bm_mp_main_loop_overrun_count(void);

/**
 * @brief 本核模块 init（按 owner_cpu 过滤）
 *
 * @return BM_OK 成功
 */
int bm_module_init_on_this_cpu(void);

/**
 * @brief 本核模块 start
 *
 * @return BM_OK 成功
 */
int bm_module_start_on_this_cpu(void);

/**
 * @brief 本核 exec init（按 owner_cpu 过滤）
 *
 * @param instances 实例表
 * @param count 实例数
 * @return BM_OK 成功
 */
int bm_exec_init_on_this_cpu(const bm_exec_t *const *instances,
                             uint32_t count);

/**
 * @brief 本核 exec prepare（assemble HRT、bind stream，不启 IRQ）
 *
 * @param instances 实例表
 * @param count 实例数
 * @return BM_OK 成功
 */
int bm_exec_prepare_on_this_cpu(const bm_exec_t *const *instances,
                                uint32_t count);

/**
 * @brief 本核 IRQ release 后启动 HRT 与外设
 *
 * @return BM_OK 成功；BM_ERR_NOT_INIT 门控未打开
 */
int bm_exec_irq_release_on_this_cpu(void);

/**
 * @brief 本核 stream 固定预算 drain（consumer → run_block → release）
 *
 * @param budget 本轮最多处理块数
 * @return 实际处理块数
 */
int bm_stream_drain_on_this_cpu(uint32_t budget);

/**
 * @brief 本核已注册 relay 的固定预算 drain
 */
int bm_stream_relay_drain_on_this_cpu(uint32_t budget);

/**
 * @brief 预指定事件类型的 owner CPU（须在 partition build 前调用）
 *
 * @param type 事件类型 ID
 * @param name 类型名称
 * @param owner_cpu 所属 CPU，或 BM_CPU_ANY 由分区器在 build 时分配
 * @return BM_OK 成功
 */
int bm_event_register_type_owner(bm_event_type_t type,
                                 const char *name,
                                 uint8_t owner_cpu);

void bm_mp_cpu_main_iteration(void);

/**
 * @brief 主循环迭代前协作钩子（弱符号；QEMU 慢仿真演示可覆盖）
 *
 * @param cpu 当前逻辑 CPU
 */
void bm_mp_coop_pre_iteration(uint32_t cpu);

#if defined(BM_CONFIG_MP_HARD_RT_PROFILE) && BM_CONFIG_MP_HARD_RT_PROFILE
/**
 * @brief hard/block realtime profile 默认 deadline miss → safe-stop
 *
 * 由 `bm_mp_init` 注册到 `bm_exec_set_deadline_miss_handler`。
 *
 * @param slot 触发 deadline 错过的槽
 * @param block 待处理块
 * @param elapsed_us 自块时间戳起已过微秒数
 */
void bm_mp_exec_deadline_safe_stop(const bm_exec_slot_t *slot,
                                   bm_block_t *block,
                                   uint32_t elapsed_us);

/**
 * @brief hard/block realtime profile 跨核 IPC 序列异常 → safe-stop
 *
 * 由 `bm_mp_init` 注册到 `bm_mp_ipc_set_fault_hook`。
 *
 * @param source_cpu 出错源 CPU
 * @param target_cpu 出错目标 CPU
 */
void bm_mp_ipc_fault_safe_stop(uint8_t source_cpu, uint8_t target_cpu);
#endif

#endif /* BM_MP_H */

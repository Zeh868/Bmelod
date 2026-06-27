/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_config.h
 * @brief 框架全局编译期配置宏
 *
 * 各子模块容量、可选组件开关及混合域参数均在此集中定义。
 * 应用可通过项目级 bm_config.h 覆盖默认值。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_CONFIG_H
#define BM_CONFIG_H

/*
 * 组件开关（与 CMake BM_ENABLE_* / PROFILE 对齐）。
 * 应用 bm_config.h 可用 #define 覆盖；CMake 集成时由 bm_config 目标注入 -D。
 * Ultra 剖面设 BM_CONFIG_ENABLE_ULTRA=1，其余组件开关忽略。
 */
#ifndef BM_CONFIG_ENABLE_ULTRA
#define BM_CONFIG_ENABLE_ULTRA               0
#endif
#ifndef BM_CONFIG_ENABLE_MODULE
#define BM_CONFIG_ENABLE_MODULE              1
#endif
#ifndef BM_CONFIG_ENABLE_CHANNEL
#define BM_CONFIG_ENABLE_CHANNEL             0
#endif
#ifndef BM_CONFIG_ENABLE_SHELL
#define BM_CONFIG_ENABLE_SHELL               0
#endif
#ifndef BM_CONFIG_ENABLE_WDG
#define BM_CONFIG_ENABLE_WDG                 1
#endif
#ifndef BM_CONFIG_ENABLE_HRT
#define BM_CONFIG_ENABLE_HRT                 0
#endif
#ifndef BM_CONFIG_ENABLE_TICKER
#define BM_CONFIG_ENABLE_TICKER              0
#endif
#ifndef BM_CONFIG_ENABLE_EXEC
#define BM_CONFIG_ENABLE_EXEC           0
#endif
#ifndef BM_CONFIG_ENABLE_SYNC
#define BM_CONFIG_ENABLE_SYNC                0
#endif
#ifndef BM_CONFIG_ENABLE_STREAM
#define BM_CONFIG_ENABLE_STREAM              0
#endif
#ifndef BM_CONFIG_ENABLE_PIPELINE
#define BM_CONFIG_ENABLE_PIPELINE            0
#endif
#ifndef BM_CONFIG_ENABLE_ALGORITHM
#define BM_CONFIG_ENABLE_ALGORITHM           0
#endif

/* 流式域（Block/Frame RT） */
#ifndef BM_CONFIG_STREAM_MAX_BLOCKS
#define BM_CONFIG_STREAM_MAX_BLOCKS          4u
#endif
#if BM_CONFIG_STREAM_MAX_BLOCKS < 2u || BM_CONFIG_STREAM_MAX_BLOCKS > 32u
#error "BM_CONFIG_STREAM_MAX_BLOCKS must be in [2, 32]"
#endif

#ifndef BM_CONFIG_ENABLE_LOG
#define BM_CONFIG_ENABLE_LOG                 1
#endif
#ifndef BM_CONFIG_LOG_LEVEL
#define BM_CONFIG_LOG_LEVEL                  2   /* BM_LOG_INFO (E=0,W=1,I=2,D=3,T=4) */
#endif
#ifndef BM_CONFIG_LOG_BUF_SIZE
#define BM_CONFIG_LOG_BUF_SIZE               128
#endif
#ifndef BM_CONFIG_LOG_USE_STDIO
#define BM_CONFIG_LOG_USE_STDIO              0
#endif

/* Console 诊断 I/O（日志 / Shell 分通道，编译期选后端） */
#ifndef BM_CONSOLE_BACKEND_NONE
#define BM_CONSOLE_BACKEND_NONE                0
#endif
#ifndef BM_CONSOLE_BACKEND_STDIO
#define BM_CONSOLE_BACKEND_STDIO               1
#endif
#ifndef BM_CONSOLE_BACKEND_UART
#define BM_CONSOLE_BACKEND_UART                2
#endif
#ifndef BM_CONSOLE_BACKEND_RTT
#define BM_CONSOLE_BACKEND_RTT                 3
#endif

#ifndef BM_CONFIG_CONSOLE_LOG_BACKEND
#if BM_CONFIG_LOG_USE_STDIO
#define BM_CONFIG_CONSOLE_LOG_BACKEND          BM_CONSOLE_BACKEND_STDIO
#else
#define BM_CONFIG_CONSOLE_LOG_BACKEND          BM_CONSOLE_BACKEND_NONE
#endif
#endif

#ifndef BM_CONFIG_CONSOLE_CLI_BACKEND
#define BM_CONFIG_CONSOLE_CLI_BACKEND          BM_CONFIG_CONSOLE_LOG_BACKEND
#endif

#ifndef BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY
#define BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY  1
#endif

#if BM_CONFIG_HARD_RT_PROFILE && \
    (BM_CONFIG_CONSOLE_LOG_BACKEND == BM_CONSOLE_BACKEND_STDIO || \
     BM_CONFIG_CONSOLE_CLI_BACKEND == BM_CONSOLE_BACKEND_STDIO)
#error "hard RT profile forbids stdio console backend"
#endif

/* 核心事件子系统 */
#ifndef BM_CONFIG_MAX_EVENT_TYPES
#define BM_CONFIG_MAX_EVENT_TYPES           16
#endif
#ifndef BM_CONFIG_MAX_EVENT_SUBSCRIBERS
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     32
#endif
#ifndef BM_CONFIG_EVENT_QUEUE_SIZE
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#endif
#ifndef BM_CONFIG_EVENT_PRIORITIES
#define BM_CONFIG_EVENT_PRIORITIES          4
#endif
/* EVENT_QUEUE_SIZE 须能被 EVENT_PRIORITIES 整除，且商为 2 的幂 */
#if (BM_CONFIG_EVENT_QUEUE_SIZE % BM_CONFIG_EVENT_PRIORITIES) != 0
#error "BM_CONFIG_EVENT_QUEUE_SIZE must be divisible by BM_CONFIG_EVENT_PRIORITIES"
#endif
#if ((BM_CONFIG_EVENT_QUEUE_SIZE / BM_CONFIG_EVENT_PRIORITIES) < 2) || \
    (((BM_CONFIG_EVENT_QUEUE_SIZE / BM_CONFIG_EVENT_PRIORITIES) & \
      ((BM_CONFIG_EVENT_QUEUE_SIZE / BM_CONFIG_EVENT_PRIORITIES) - 1)) != 0)
#error "BM_CONFIG_EVENT_QUEUE_SIZE / BM_CONFIG_EVENT_PRIORITIES must be >= 2 and a power of 2"
#endif

#ifndef BM_CONFIG_EVENT_INLINE_DATA_SIZE
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE     8
#endif
#ifndef BM_CONFIG_EVENT_PRIORITY_BURST_MAX
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX   8
#endif

/* 可选组件 */
#ifndef BM_CONFIG_MAX_MODULES
#define BM_CONFIG_MAX_MODULES                8
#endif
#ifndef BM_CONFIG_SHELL_BUF_SIZE
#define BM_CONFIG_SHELL_BUF_SIZE            64
#endif
#ifndef BM_CONFIG_SHELL_MAX_ARGS
#define BM_CONFIG_SHELL_MAX_ARGS             4
#endif
#ifndef BM_CONFIG_SHELL_MAX_CMDS
#define BM_CONFIG_SHELL_MAX_CMDS             8
#endif
#ifndef BM_CONFIG_SHELL_MAX_NAME_LEN
#define BM_CONFIG_SHELL_MAX_NAME_LEN         16
#endif
#ifndef BM_CONFIG_MAX_WDG_MODULES
#define BM_CONFIG_MAX_WDG_MODULES            4
#endif
#ifndef BM_CONFIG_WDG_MODULE_TIMEOUT_MS
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS      1000
#endif
#ifndef BM_CONFIG_WDG_MAX_NAME_LEN
#define BM_CONFIG_WDG_MAX_NAME_LEN           32
#endif

/* 持久化 KV 存储（路线图 #10） */
/** 最大 KV 条目数（RAM 表容量）*/
#ifndef BM_CONFIG_PERSIST_MAX_ENTRIES
#define BM_CONFIG_PERSIST_MAX_ENTRIES        16u
#endif
/** 键名最大长度（不含 null 终止符，字节数）*/
#ifndef BM_CONFIG_PERSIST_KEY_MAX_LEN
#define BM_CONFIG_PERSIST_KEY_MAX_LEN        15u
#endif
/** 值最大字节数 */
#ifndef BM_CONFIG_PERSIST_VAL_MAX_LEN
#define BM_CONFIG_PERSIST_VAL_MAX_LEN        64u
#endif

/* Ultra 超轻量剖面（header-only） */
#ifndef BM_CONFIG_ULTRA_MAX_EVENT_TYPES
#define BM_CONFIG_ULTRA_MAX_EVENT_TYPES      8
#endif
#ifndef BM_CONFIG_ULTRA_QUEUE_DEPTH
#define BM_CONFIG_ULTRA_QUEUE_DEPTH          8
#endif
#ifndef BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE
#define BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE  8
#endif

/* 混合域（可选） */
#ifndef BM_CONFIG_ENABLE_PRIORITY_MASK
#define BM_CONFIG_ENABLE_PRIORITY_MASK       0
#endif
#ifndef BM_CONFIG_HRT_PRIORITY_THRESHOLD
#define BM_CONFIG_HRT_PRIORITY_THRESHOLD     4
#endif
#ifndef BM_CONFIG_HRT_TICK_US
#define BM_CONFIG_HRT_TICK_US                100
#endif
#ifndef BM_CONFIG_HRT_MAX_SLOTS
#define BM_CONFIG_HRT_MAX_SLOTS              16
#endif
#ifndef BM_CONFIG_HRT_DISPATCH_PER_ISR
#define BM_CONFIG_HRT_DISPATCH_PER_ISR       BM_CONFIG_HRT_MAX_SLOTS
#endif
#ifndef BM_CONFIG_TICKER_MAX_SLOTS
#define BM_CONFIG_TICKER_MAX_SLOTS           8
#endif
#ifndef BM_CONFIG_TICKER_MAX_CATCHUP
#define BM_CONFIG_TICKER_MAX_CATCHUP         4
#endif
#ifndef BM_CONFIG_MAX_EXEC_SLOTS
#define BM_CONFIG_MAX_EXEC_SLOTS             32
#endif
#ifndef BM_CONFIG_MAX_EXEC_INSTANCES
#define BM_CONFIG_MAX_EXEC_INSTANCES         16
#endif
#ifndef BM_CONFIG_MAX_RESOURCE_CLAIMS
#define BM_CONFIG_MAX_RESOURCE_CLAIMS        64
#endif
#ifndef BM_CONFIG_MAX_SYNC_MEMBERS
#define BM_CONFIG_MAX_SYNC_MEMBERS           BM_CONFIG_MAX_EXEC_INSTANCES
#endif
#ifndef BM_CONFIG_SYNC_MAX_PHASE_TICKS
#define BM_CONFIG_SYNC_MAX_PHASE_TICKS       1000000000u
#endif

/* 单核运行与 cache-line 默认值 */
#ifndef BM_CONFIG_CPU_COUNT
#define BM_CONFIG_CPU_COUNT                  1u
#endif
#ifndef BM_CONFIG_CACHE_LINE
#define BM_CONFIG_CACHE_LINE                 64
#endif
#ifndef BM_CONFIG_EVENT_PROCESS_BUDGET
#define BM_CONFIG_EVENT_PROCESS_BUDGET       16u
#endif
#ifndef BM_CONFIG_STREAM_DRAIN_BUDGET
#define BM_CONFIG_STREAM_DRAIN_BUDGET        4u
#endif
#ifndef BM_CONFIG_RELAY_DRAIN_BUDGET
#define BM_CONFIG_RELAY_DRAIN_BUDGET           4u
#endif

#ifndef BM_CONFIG_RELAY_REGISTRY_MAX
#define BM_CONFIG_RELAY_REGISTRY_MAX           4u
#endif

#ifndef BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US
#define BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US  50u
#endif

#ifndef BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US
#define BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US    40u
#endif

#ifndef BM_CONFIG_MAIN_LOOP_PERIOD_US
#define BM_CONFIG_MAIN_LOOP_PERIOD_US             1000u
#endif

#ifndef BM_CONFIG_MAIN_LOOP_MAX_SPINS
#define BM_CONFIG_MAIN_LOOP_MAX_SPINS           100000u
#endif

#ifndef BM_CONFIG_MAIN_LOOP_FIXED_OVERHEAD_US
#define BM_CONFIG_MAIN_LOOP_FIXED_OVERHEAD_US      4u
#endif

#ifndef BM_CONFIG_MAIN_LOOP_DEADLINE_US
#define BM_CONFIG_MAIN_LOOP_DEADLINE_US          5000u
#endif

#ifndef BM_CONFIG_BOOT_BARRIER_TIMEOUT_US
#define BM_CONFIG_BOOT_BARRIER_TIMEOUT_US     5000000u
#endif

#ifndef BM_CONFIG_IPC_EVENT_RING_DEPTH
#define BM_CONFIG_IPC_EVENT_RING_DEPTH       8u
#endif
#ifndef BM_CONFIG_IPC_PER_SOURCE_BUDGET
#define BM_CONFIG_IPC_PER_SOURCE_BUDGET      4u
#endif
#ifndef BM_CONFIG_IPC_DRAIN_BUDGET
#define BM_CONFIG_IPC_DRAIN_BUDGET  \
    ((BM_CONFIG_CPU_COUNT > 1u) ? \
     ((BM_CONFIG_CPU_COUNT - 1u) * BM_CONFIG_IPC_PER_SOURCE_BUDGET) : 0u)
#endif

#ifndef BM_CONFIG_WDG_HB_TIMEOUT_MS
#define BM_CONFIG_WDG_HB_TIMEOUT_MS          500u
#endif

#ifndef BM_CONFIG_RESOURCE_TOPOLOGY_MAX
#define BM_CONFIG_RESOURCE_TOPOLOGY_MAX      32u
#endif

#ifndef BM_CONFIG_EXPERIMENTAL_STREAM
#define BM_CONFIG_EXPERIMENTAL_STREAM        0u
#endif

#ifndef BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED
#define BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED  \
    (BM_CONFIG_HARD_RT_PROFILE)
#endif

#ifndef BM_CONFIG_IPC_MEMORY_COHERENT_OR_NONCACHEABLE
#define BM_CONFIG_IPC_MEMORY_COHERENT_OR_NONCACHEABLE  1u
#endif

#ifndef BM_CONFIG_HARD_RT_PROFILE
#define BM_CONFIG_HARD_RT_PROFILE                0
#endif

#ifndef BM_CONFIG_LOG_RING_DEPTH
#define BM_CONFIG_LOG_RING_DEPTH                 16u
#endif

#ifndef BM_CONFIG_LOG_RING
#define BM_CONFIG_LOG_RING                       0
#endif

#ifndef BM_CONFIG_LOG_DRAIN_BUDGET
#define BM_CONFIG_LOG_DRAIN_BUDGET               2u
#endif

#ifndef BM_CONFIG_LOG_DRAIN_WCET_PER_MSG_US
#define BM_CONFIG_LOG_DRAIN_WCET_PER_MSG_US      100u
#endif

#ifndef BM_CONFIG_RTA_MAX_ITERATIONS
#define BM_CONFIG_RTA_MAX_ITERATIONS             128u
#endif

#ifndef BM_CONFIG_ATOMIC_MAX_RETRIES
#define BM_CONFIG_ATOMIC_MAX_RETRIES              8u
#endif

#if BM_CONFIG_ATOMIC_MAX_RETRIES < 1u
#error "BM_CONFIG_ATOMIC_MAX_RETRIES must be at least 1"
#endif

/*
 * bm_bus LATEST 读路径 spin-until-stable 的重试上界（DET-01）。
 * 写者在读窗口内持续覆盖发布时，至多重试 N 次后非阻塞返回
 * BM_ERR_WOULD_BLOCK，保证 acquire_read 的 WCET 可静态分析。
 * 与 BM_CONFIG_ATOMIC_MAX_RETRIES 同源策略（CAS 环亦有界）。
 */
#ifndef BM_CONFIG_BUS_LATEST_MAX_RETRIES
#define BM_CONFIG_BUS_LATEST_MAX_RETRIES          8u
#endif

#if BM_CONFIG_BUS_LATEST_MAX_RETRIES < 1u
#error "BM_CONFIG_BUS_LATEST_MAX_RETRIES must be at least 1"
#endif

#ifndef BM_CONFIG_BOOTSTRAP_CPU
#define BM_CONFIG_BOOTSTRAP_CPU                  0u
#endif

#endif /* BM_CONFIG_H */

/**
 * @file bm_config_mp_hardrt.h
 * @brief 双核 PERCPU hard RT 剖面测试配置（HARD_RT_PROFILE=1）
 *
 * 与 Demo/common/bm_config_mp_demo.h 一致，仅将 BM_CONFIG_HARD_RT_PROFILE
 * 置 1，使整套单测在 hard RT 代码路径下回归：profile build 闭包、stream gate
 * 强制校验、IPC 序列异常 fail-closed、boot attach 顺序约束等分支。
 *
 * native HAL 满足 hard RT 能力闭包的前提：
 *   - BM_HAL_CRITICAL_MASKS_STREAM_IRQ=1（提供 stream IRQ 屏蔽能力）；
 *   - 缓存默认 noop、IPC 内存默认 coherent；
 *   - LOG_RING 深度默认 16（≥4）。
 *
 * 由 CMake -include 强制预包含，勿定义 BM_CONFIG_H。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            正式发布
 *
 */
#ifndef BM_CONFIG_MP_HARDRT_H
#define BM_CONFIG_MP_HARDRT_H

#define BM_CONFIG_CPU_COUNT                  2u
#define BM_CONFIG_TOPOLOGY                   3u   /* BM_MP_PERCPU */

#define BM_CONFIG_MAX_EVENT_TYPES           16
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     32
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#define BM_CONFIG_EVENT_PRIORITIES          4
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX  8u
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE     8
#define BM_CONFIG_MAX_MODULES                8
#define BM_CONFIG_MAX_WDG_MODULES            4
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS      1000
#define BM_CONFIG_WDG_MAX_NAME_LEN           32

#define BM_CONFIG_HRT_TICK_US               100
#define BM_CONFIG_HRT_MAX_SLOTS             16
#define BM_CONFIG_TICKER_MAX_SLOTS          8
#define BM_CONFIG_TICKER_MAX_CATCHUP        4
#define BM_CONFIG_MAX_EXEC_SLOTS            32
#define BM_CONFIG_MAX_EXEC_INSTANCES        8
#define BM_CONFIG_MAX_RESOURCE_CLAIMS       32
#define BM_CONFIG_STREAM_MAX_BLOCKS         4u
#define BM_CONFIG_STREAM_RELAY_MAX_DEPTH    4u

#define BM_CONFIG_ENABLE_IPC                 1
#define BM_CONFIG_IPC_CACHE_LINE             128u
#define BM_CONFIG_IPC_REL_CMD_CAPACITY       8u
#define BM_CONFIG_IPC_MAX_PAYLOAD_WORDS      16u
#define BM_CONFIG_IPC_RT_HB_TIMEOUT_MS       100u
#define BM_CONFIG_CACHE_LINE                 64u
#define BM_CONFIG_BOOT_BARRIER_TIMEOUT_US 5000000u
#define BM_CONFIG_IPC_EVENT_RING_DEPTH    8u
#define BM_CONFIG_IPC_PER_SOURCE_BUDGET   4u
#define BM_CONFIG_RELAY_REGISTRY_MAX       8u
#define BM_CONFIG_RELAY_DRAIN_BUDGET        4u
#define BM_CONFIG_EVENT_PROCESS_BUDGET      16u
#define BM_CONFIG_STREAM_DRAIN_BUDGET       4u
#define BM_CONFIG_LOG_DRAIN_BUDGET          1u
#define BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US  50u
#define BM_CONFIG_IPC_DRAIN_WCET_PER_MSG_US      8u
#define BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US   40u
#define BM_CONFIG_MAIN_LOOP_PERIOD_US           1000u
#define BM_CONFIG_MAIN_LOOP_DEADLINE_US         5000u
#define BM_CONFIG_IPC_DRAIN_BUDGET  \
    ((BM_CONFIG_CPU_COUNT > 1u) ? \
     ((BM_CONFIG_CPU_COUNT - 1u) * BM_CONFIG_IPC_PER_SOURCE_BUDGET) : 0u)

#define BM_CONFIG_SCHEDULE_MAX_SLOTS     16u
#define BM_CONFIG_WDG_HB_TIMEOUT_MS      500u
#define BM_CONFIG_HARD_RT_PROFILE        1u   /* hard RT 剖面：与 demo 唯一差异 */
#define BM_CONFIG_EXPERIMENTAL_STREAM    0u

#define BM_CONFIG_ENABLE_LOG                 1
#define BM_CONFIG_LOG_LEVEL                  3
#define BM_CONFIG_LOG_RING                1
#define BM_CONFIG_LOG_USE_STDIO              0
#define BM_CONFIG_CONSOLE_LOG_BACKEND          0   /* hard RT：日志经 ring drain，单测覆盖 bm_log_output */
#define BM_CONFIG_CONSOLE_CLI_BACKEND          0   /* hard RT：CLI 通道关闭 */
#define BM_HAL_CRITICAL_MASKS_STREAM_IRQ     1

#endif /* BM_CONFIG_MP_HARDRT_H */

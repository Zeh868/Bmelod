/**
 * @file bm_config_qemu_smp.h
 * @brief dual_core_stream QEMU RISC-V64 SMP 绀轰緥閰嶇疆
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 淇敼鏃ュ織:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            姝ｅ紡鍙戝竷
 *
 */
/* 鐢?CMake -include 寮哄埗棰勫寘鍚紱鍕垮畾涔?BM_CONFIG_H锛屼互渚块殢鍚庢媺鍏ラ粯璁ら厤缃?*/

#define BM_CONFIG_CPU_COUNT                  2u
#define BM_CONFIG_TOPOLOGY                3u   /* BM_MP_PERCPU */

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

#define BM_CONFIG_ULTRA_MAX_EVENT_TYPES      8
#define BM_CONFIG_ULTRA_QUEUE_DEPTH          8
#define BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE  8

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
#define BM_CONFIG_BOOT_BARRIER_TIMEOUT_US 30000000u
#define BM_CONFIG_IPC_EVENT_RING_DEPTH    8u
#define BM_CONFIG_IPC_PER_SOURCE_BUDGET   4u
#define BM_CONFIG_RELAY_REGISTRY_MAX       4u
#define BM_CONFIG_RELAY_DRAIN_BUDGET        4u
#define BM_CONFIG_EVENT_PROCESS_BUDGET      16u
#define BM_CONFIG_STREAM_DRAIN_BUDGET       4u
#define BM_CONFIG_STREAM_DRAIN_WCET_PER_BLOCK_US  50u
#define BM_CONFIG_IPC_DRAIN_WCET_PER_MSG_US      8u
#define BM_CONFIG_RELAY_DRAIN_WCET_PER_SLOT_US   40u
/* QEMU 浠跨湡鎷夐暱涓诲惊鐜懆鏈燂紝閬垮厤闈欐€佽皟搴﹀埄鐢ㄧ巼鏍￠獙婧㈠嚭 */
#define BM_CONFIG_MAIN_LOOP_PERIOD_US           10000u
#define BM_CONFIG_MAIN_LOOP_DEADLINE_US         50000u
#define BM_CONFIG_IPC_DRAIN_BUDGET  \
    ((BM_CONFIG_CPU_COUNT > 1u) ? \
     ((BM_CONFIG_CPU_COUNT - 1u) * BM_CONFIG_IPC_PER_SOURCE_BUDGET) : 0u)

#define BM_CONFIG_SCHEDULE_MAX_SLOTS     16u
#define BM_CONFIG_WDG_HB_TIMEOUT_MS      500u
#define BM_CONFIG_HARD_RT_PROFILE        1u
#define BM_CONFIG_IPC_MEMORY_COHERENT_OR_NONCACHEABLE 1u
#define BM_CONFIG_EXPERIMENTAL_STREAM    0u
#define BM_HAL_CRITICAL_MASKS_STREAM_IRQ 1

#define BM_CONFIG_ENABLE_LOG                 0

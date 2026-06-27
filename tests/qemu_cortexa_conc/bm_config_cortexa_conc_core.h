/**
 * @file bm_config_cortexa_conc_core.h
 * @brief core 层并发测试（atomic_ipc/critical/mempool/event）QEMU Cortex-A15 SMP 配置
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 */
#ifndef BM_CONFIG_CORTEXA_CONC_CORE_H
#define BM_CONFIG_CORTEXA_CONC_CORE_H

#define BM_CONFIG_CPU_COUNT          2u

#define BM_CONFIG_ENABLE_LOG         0
#define BM_CONFIG_ENABLE_IPC         0   /* event 用桩 forwarder，不需真 IPC 模块 */

#define BM_CONFIG_MAX_EVENT_TYPES         4
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS   4
#define BM_CONFIG_EVENT_QUEUE_SIZE        4
#define BM_CONFIG_EVENT_PRIORITIES        2
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX 4u
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE   8
#define BM_CONFIG_MAX_MODULES              2
#define BM_CONFIG_MAX_WDG_MODULES          1
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS    1000
#define BM_CONFIG_WDG_MAX_NAME_LEN         16

#define BM_CONFIG_ULTRA_MAX_EVENT_TYPES     2
#define BM_CONFIG_ULTRA_QUEUE_DEPTH         2
#define BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE 4

#define BM_CONFIG_CACHE_LINE         64u
#define BM_CONFIG_IPC_CACHE_LINE     64u

#define BM_CONSOLE_BACKEND_UART      2
#define BM_CONFIG_CONSOLE_LOG_BACKEND BM_CONSOLE_BACKEND_UART
#define BM_CONFIG_CONSOLE_CLI_BACKEND BM_CONSOLE_BACKEND_UART

#define BM_HAL_CRITICAL_MASKS_STREAM_IRQ 1

#endif /* BM_CONFIG_CORTEXA_CONC_CORE_H */

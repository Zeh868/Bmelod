/**
 * @file bm_config_cortexa_bus_smp.h
 * @brief bm_bus 多核 QEMU Cortex-A15 SMP 验证专用配置头
 *
 * 仅启用 bm_bus 测试所需最小配置：
 * - 双核（BM_CONFIG_CPU_COUNT=2）
 * - 使能 C11 原子（bm_atomic_ipc 依赖）
 * - 关闭所有可选子系统（降低链接依赖）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿
 *
 */
#ifndef BM_CONFIG_CORTEXA_BUS_SMP_H
#define BM_CONFIG_CORTEXA_BUS_SMP_H

/* 双核 SMP */
#define BM_CONFIG_CPU_COUNT          2u

/* 关闭日志/IPC */
#define BM_CONFIG_ENABLE_LOG         0
#define BM_CONFIG_ENABLE_IPC         0

/* 事件系统最小配置（bm_core 链接依赖） */
#define BM_CONFIG_MAX_EVENT_TYPES         4
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS   4
#define BM_CONFIG_EVENT_QUEUE_SIZE        4
#define BM_CONFIG_EVENT_PRIORITIES        2
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX 4u
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE   4
#define BM_CONFIG_MAX_MODULES              2
#define BM_CONFIG_MAX_WDG_MODULES          1
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS    1000
#define BM_CONFIG_WDG_MAX_NAME_LEN         16

/* ULTRA 最小配置 */
#define BM_CONFIG_ULTRA_MAX_EVENT_TYPES     2
#define BM_CONFIG_ULTRA_QUEUE_DEPTH         2
#define BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE 4

/* 缓存行（Cortex-A15 典型值 64 字节） */
#define BM_CONFIG_CACHE_LINE         64u
#define BM_CONFIG_IPC_CACHE_LINE     64u

/* console/UART 后端（PL011） */
#define BM_CONSOLE_BACKEND_UART      2
#define BM_CONFIG_CONSOLE_LOG_BACKEND BM_CONSOLE_BACKEND_UART
#define BM_CONFIG_CONSOLE_CLI_BACKEND BM_CONSOLE_BACKEND_UART

/* 使能 bm_bus */
#define BM_ENABLE_BUS                1

/* 使能 bm_stream（BLOCK 模式 adapter 依赖） */
#define BM_ENABLE_STREAM             1

/* 临界区屏蔽流式 IRQ */
#define BM_HAL_CRITICAL_MASKS_STREAM_IRQ 1

#endif /* BM_CONFIG_CORTEXA_BUS_SMP_H */

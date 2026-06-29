/**
 * @file bm_config_app.h
 * @brief bus_servo 示例运行时容量配置
 *
 * 与 closed_loop_servo 保持一致，额外确认 bm_bus 不需要额外容量宏。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿，从 closed_loop_servo 复制并适配
 *
 */
/* 由 CMake -include 强制预包含；勿定义 BM_CONFIG_H */

#define BM_CONFIG_MAX_EVENT_TYPES           16
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     32
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#define BM_CONFIG_EVENT_PRIORITIES          4
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE     8
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
#define BM_CONFIG_MAX_SYNC_MEMBERS          BM_CONFIG_MAX_EXEC_INSTANCES
#define BM_CONFIG_SYNC_MAX_PHASE_TICKS      1000000000u
#define BM_CONFIG_ENABLE_PRIORITY_MASK      0

/**
 * @file bm_config_app.h
 * @brief tinyml_graph 示例运行时容量配置
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始版本
 *
 */
/* 由 CMake -include 强制预包含；勿定义 BM_CONFIG_H */

#define BM_CONFIG_MAX_EVENT_TYPES           8
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     8
#define BM_CONFIG_EVENT_QUEUE_SIZE          8
#define BM_CONFIG_EVENT_PRIORITIES          2
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE     8
#define BM_CONFIG_HRT_TICK_US               1000
#define BM_CONFIG_HRT_MAX_SLOTS             4
#define BM_CONFIG_TICKER_MAX_SLOTS          4
#define BM_CONFIG_TICKER_MAX_CATCHUP        2
#define BM_CONFIG_MAX_EXEC_SLOTS            4
#define BM_CONFIG_MAX_EXEC_INSTANCES        2
#define BM_CONFIG_MAX_RESOURCE_CLAIMS       4
#define BM_CONFIG_ENABLE_PRIORITY_MASK      0

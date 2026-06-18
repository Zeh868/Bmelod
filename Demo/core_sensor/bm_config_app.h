/**
 * @file bm_config_app.h
 * @brief core_sensor 示例运行时容量配置
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 *  2026-06-10       1.0            zeh            正式发布
 *
 */
/* 由 CMake -include 强制预包含；勿定义 BM_CONFIG_H */

#define BM_CONFIG_MAX_EVENT_TYPES           8
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     16
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#define BM_CONFIG_EVENT_PRIORITIES          4
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE    16
#define BM_CONFIG_MAX_MODULES               4
#define BM_CONFIG_MEMPOOL_MAX_POOLS         2


/**
 * @file bm_config_app.h
 * @brief QEMU 测试专用 bm_config 覆盖：事件队列与 HRT 参数
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

/* 由 CMake -include 强制预包含；勿定义 BM_CONFIG_H */

#define BM_CONFIG_MAX_EVENT_TYPES           16
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS     16
#define BM_CONFIG_EVENT_QUEUE_SIZE          16
#define BM_CONFIG_EVENT_PRIORITIES          4
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE     8
#define BM_CONFIG_ENABLE_PRIORITY_MASK      0
#define BM_CONFIG_HRT_TICK_US               1000
#define BM_CONFIG_HRT_MAX_SLOTS             4

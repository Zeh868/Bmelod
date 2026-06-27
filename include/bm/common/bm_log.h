/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_log.h
 * @brief 分级日志接口（ERROR / WARN / INFO / DEBUG / TRACE）
 *
 * 通过 BM_CONFIG_ENABLE_LOG 与 BM_CONFIG_LOG_LEVEL 在编译期裁剪。
 * hard RT 剖面下 BM_LOG* 在宏层直接裁剪，不进入格式化/stdio 路径。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            per-CPU 有界 ring
 * 2026-06-15       1.2            zeh            hard RT 日志宏裁剪
 *
 */
#ifndef BM_LOG_H
#define BM_LOG_H

#include <stddef.h>

typedef enum {
    BM_LOG_ERROR = 0,
    BM_LOG_WARN  = 1,
    BM_LOG_INFO  = 2,
    BM_LOG_DEBUG = 3,
    BM_LOG_TRACE = 4
} bm_log_level_t;

#ifndef BM_CONFIG_ENABLE_LOG
#define BM_CONFIG_ENABLE_LOG 1
#endif

#ifndef BM_CONFIG_LOG_LEVEL
#define BM_CONFIG_LOG_LEVEL BM_LOG_INFO
#endif

#ifndef BM_CONFIG_LOG_BUF_SIZE
#define BM_CONFIG_LOG_BUF_SIZE 128
#endif

#ifndef BM_CONFIG_LOG_USE_STDIO
#define BM_CONFIG_LOG_USE_STDIO 0
#endif

#ifndef BM_CONFIG_LOG_RING_DEPTH
#define BM_CONFIG_LOG_RING_DEPTH 16u
#endif

#ifndef BM_CONFIG_LOG_RING
#define BM_CONFIG_LOG_RING 0
#endif

#ifndef BM_CONFIG_HARD_RT_PROFILE
#define BM_CONFIG_HARD_RT_PROFILE 0
#endif

#ifndef BM_CONFIG_LOG_DRAIN_BUDGET
#define BM_CONFIG_LOG_DRAIN_BUDGET 2u
#endif

void bm_log(bm_log_level_t level, const char *tag, const char *fmt, ...);
void bm_log_output(const char *buf, size_t len);

/**
 * @brief hard RT 剖面是否满足日志 ring 配置
 */
int bm_log_mp_profile_ok(void);

#if BM_CONFIG_ENABLE_LOG && BM_CONFIG_LOG_RING
#include <stdint.h>

/**
 * @brief 按预算 drain 指定 CPU 的日志 ring 到 `bm_log_output`
 *
 * @param cpu    逻辑 CPU 编号
 * @param budget 本轮最多输出条数
 * @return 实际输出条数
 */
uint32_t bm_log_drain_cpu(uint32_t cpu, uint32_t budget);

/**
 * @brief drain 本核日志 ring
 */
uint32_t bm_log_drain_on_this_cpu(uint32_t budget);

/**
 * @brief 查询指定 CPU 因 ring 满而丢弃的日志条数
 */
uint32_t bm_log_drop_count(uint32_t cpu);
#endif

#if BM_CONFIG_ENABLE_LOG && !BM_CONFIG_HARD_RT_PROFILE

#define BM_LOG(level, tag, ...) do { \
        if ((level) <= BM_CONFIG_LOG_LEVEL) { \
            bm_log((level), (tag), __VA_ARGS__); \
        } \
    } while (0)

#define BM_LOGE(tag, ...) BM_LOG(BM_LOG_ERROR, tag, __VA_ARGS__)
#define BM_LOGW(tag, ...) BM_LOG(BM_LOG_WARN,  tag, __VA_ARGS__)
#define BM_LOGI(tag, ...) BM_LOG(BM_LOG_INFO,  tag, __VA_ARGS__)
#define BM_LOGD(tag, ...) BM_LOG(BM_LOG_DEBUG, tag, __VA_ARGS__)
#define BM_LOGT(tag, ...) BM_LOG(BM_LOG_TRACE, tag, __VA_ARGS__)

#else

#define BM_LOG(level, tag, ...)   ((void)0)
#define BM_LOGE(tag, ...)         ((void)0)
#define BM_LOGW(tag, ...)         ((void)0)
#define BM_LOGI(tag, ...)         ((void)0)
#define BM_LOGD(tag, ...)         ((void)0)
#define BM_LOGT(tag, ...)         ((void)0)

#endif /* BM_CONFIG_ENABLE_LOG */

#endif /* BM_LOG_H */

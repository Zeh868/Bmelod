/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_ultra.h
 * @brief 超轻量事件队列（单 TU 队列 + 编译期回调表）
 *
 * 队列状态在 bm_ultra.c 中单例实现；回调表须在单个 .c 中实例化。
 *
 * @core_affinity 本核（per-CPU）
 * 每核独立 ultra 队列实例，bm_ultra_publish/pop/process 仅操作调用者所在 CPU。
 * 事件通知请使用 bm_event（自动路由）。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 * 2026-08-01       1.0            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_ULTRA_H
#define BM_ULTRA_H

#include "bm_config.h"
#include "bm/common/bm_types.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint8_t bm_event_type_t;

#if defined(_MSC_VER)
#define BM_ULTRA_ALIGNAS(bytes) __declspec(align(bytes))
#elif defined(__GNUC__) || defined(__clang__)
#define BM_ULTRA_ALIGNAS(bytes) __attribute__((aligned(bytes)))
#else
#error "bm_ultra requires compiler support for explicit data alignment"
#endif

/** 队列元素（内联固定长度数据） */
typedef struct {
    bm_event_type_t event_type;
    BM_ULTRA_ALIGNAS(8) uint8_t data[BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE];
    uint8_t         data_len;
} bm_ultra_queue_item_t;

#undef BM_ULTRA_ALIGNAS

/** 事件分发回调 */
typedef void (*bm_ultra_callback_t)(const void *data, uint8_t len);

/** 编译期定义回调表（须在单个 .c 文件中实例化） */
#define BM_ULTRA_CALLBACK_TABLE_DEFINE(...) \
    const bm_ultra_callback_t _bm_ultra_callbacks[BM_CONFIG_ULTRA_MAX_EVENT_TYPES] = { __VA_ARGS__ }

#define BM_ULTRA_CB(event_type, callback) \
    [event_type] = callback

/** 环形队列控制块 */
typedef struct {
    bm_ultra_queue_item_t items[BM_CONFIG_ULTRA_QUEUE_DEPTH];
    uint8_t write_idx;
    uint8_t read_idx;
} bm_ultra_queue_t;

/**
 * @brief 向当前 CPU 的 ultra 队列压入一个事件
 *
 * @param item 待压入事件项
 * @return BM_OK 成功；BM_ERR_OVERFLOW 队列已满；BM_ERR_BUSY 处于禁止的
 *         HRT 上下文；BM_ERR_INVALID 参数或队列状态无效
 */
int      bm_ultra_queue_push(const bm_ultra_queue_item_t *item);

/**
 * @brief 从当前 CPU 的 ultra 队列弹出一个事件
 *
 * @param item 输出事件项
 * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 队列为空；BM_ERR_BUSY 处于禁止的
 *         HRT 上下文；BM_ERR_INVALID 参数或队列状态无效
 */
int      bm_ultra_queue_pop(bm_ultra_queue_item_t *item);

/**
 * @brief 重置当前 CPU 的 ultra 队列与统计计数
 */
void     bm_ultra_queue_reset(void);

/**
 * @brief 查询当前 CPU 因队列满而丢弃的事件数
 *
 * @return 丢弃计数；当前 CPU 无效时返回 0
 */
uint32_t bm_ultra_get_dropped_count(void);

/**
 * @brief 查询当前 CPU 分发时跳过的事件数
 *
 * @return 跳过计数；当前 CPU 无效时返回 0
 */
uint32_t bm_ultra_get_dispatch_skipped_count(void);

/**
 * @brief 查询当前 CPU 的待处理事件数
 *
 * @return 待处理事件数；队列状态或当前 CPU 无效时返回 0
 */
uint8_t  bm_ultra_queue_count(void);

/**
 * @brief 处理当前 CPU 队列中的至多一个事件
 *
 * @return 1 已取出一个事件；0 队列为空、上下文被拒绝或发生错误
 */
uint8_t  bm_ultra_process(void);

/**
 * @brief 获取当前 CPU 的队列状态只读指针
 *
 * 仅供调试；并发下可能撕裂，禁止在 ISR 与队列操作并行时读取。
 *
 * @return 队列状态指针；当前 CPU 无效时返回 NULL
 */
const bm_ultra_queue_t *bm_ultra_queue_state(void);

/**
 * @brief 初始化当前 CPU 的 ultra 队列
 */
static inline void bm_ultra_init(void) {
    bm_ultra_queue_reset();
}

/**
 * @brief 将数据复制并发布到当前 CPU 的 ultra 队列
 *
 * @param type 事件类型 ID
 * @param data 事件载荷；len 为 0 时可为 NULL
 * @param len 事件载荷字节数
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NO_MEM 载荷过长；
 *         BM_ERR_OVERFLOW 队列已满；BM_ERR_BUSY 处于禁止的 HRT 上下文
 */
static inline int bm_ultra_publish(bm_event_type_t type,
                                    const void *data, uint8_t len) {
    bm_ultra_queue_item_t item;

    if (type >= BM_CONFIG_ULTRA_MAX_EVENT_TYPES) {
        return BM_ERR_INVALID;
    }
    if (len > BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE) {
        return BM_ERR_NO_MEM;
    }
    if (len > 0u && data == NULL) {
        return BM_ERR_INVALID;
    }
    item.event_type = type;
    item.data_len = len;
    if (len > 0u && data != NULL) {
        memcpy(item.data, data, len);
    }
    return bm_ultra_queue_push(&item);
}

/**
 * @brief 从 SRT 域 ISR 发布 ultra 事件
 *
 * 单核下关中断临界区可重入；禁止 HRT ISR 调用。
 *
 * @param type 事件类型 ID
 * @param data 事件载荷；len 为 0 时可为 NULL
 * @param len 事件载荷字节数
 * @return 与 @ref bm_ultra_publish 相同
 */
static inline int bm_ultra_publish_from_isr(bm_event_type_t type,
                                             const void *data, uint8_t len) {
    return bm_ultra_publish(type, data, len);
}

/**
 * @brief 查询当前 CPU 的待处理事件数
 *
 * @return 待处理事件数
 */
static inline uint8_t bm_ultra_event_count(void) {
    return bm_ultra_queue_count();
}

extern const bm_ultra_callback_t _bm_ultra_callbacks[BM_CONFIG_ULTRA_MAX_EVENT_TYPES];

#ifdef BM_ENABLE_ULTRA_TEST_HOOK
/**
 * @brief 单元测试专用：绕过事件类型校验向队列注入元素
 *
 * 生产固件不得定义 BM_ENABLE_ULTRA_TEST_HOOK。
 *
 * @param item 待注入事件项
 * @return BM_OK 成功；BM_ERR_OVERFLOW 队列已满；BM_ERR_INVALID 参数或队列
 *         状态无效；BM_ERR_BUSY 处于禁止的 HRT 上下文
 */
int bm_ultra_test_inject(const bm_ultra_queue_item_t *item);
#endif

#endif /* BM_ULTRA_H */

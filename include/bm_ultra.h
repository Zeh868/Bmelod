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
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
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

int      bm_ultra_queue_push(const bm_ultra_queue_item_t *item);
int      bm_ultra_queue_pop(bm_ultra_queue_item_t *item);
void     bm_ultra_queue_reset(void);
uint32_t bm_ultra_get_dropped_count(void);
uint32_t bm_ultra_get_dispatch_skipped_count(void);
uint8_t  bm_ultra_queue_count(void);
uint8_t  bm_ultra_process(void);
/** 仅调试只读；并发下可能撕裂，禁止在 ISR 与队列操作并行时读取 */
const bm_ultra_queue_t *bm_ultra_queue_state(void);

static inline void bm_ultra_init(void) {
    bm_ultra_queue_reset();
}

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

/** SRT 域 ISR 专用：单核下关中断临界区可重入；禁止 HRT ISR 调用 */
static inline int bm_ultra_publish_from_isr(bm_event_type_t type,
                                             const void *data, uint8_t len) {
    return bm_ultra_publish(type, data, len);
}

static inline uint8_t bm_ultra_event_count(void) {
    return bm_ultra_queue_count();
}

extern const bm_ultra_callback_t _bm_ultra_callbacks[BM_CONFIG_ULTRA_MAX_EVENT_TYPES];

#ifdef BM_ENABLE_ULTRA_TEST_HOOK
/** 单元测试专用：绕过 type 校验向队列注入元素（生产固件勿定义此宏） */
int bm_ultra_test_inject(const bm_ultra_queue_item_t *item);
#endif

#endif /* BM_ULTRA_H */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_ultra.c
 * @brief Ultra 剖面事件队列实现（按 CPU 分域）
 *
 * 默认路径下全局单例队列 + 计数器；按 CPU 路由时采用分域数组，各域独立。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            分域存储，路由启用时独立
 *
 */
#include "bm_ultra.h"
#include "bm_critical_wrap.h"
#include "bm_safety.h"
#include "bm/core/bm_cpu_local.h"
#include "hal/bm_hal_cpu.h"

#include <string.h>

#if BM_CONFIG_ULTRA_QUEUE_DEPTH < 2 || \
    ((BM_CONFIG_ULTRA_QUEUE_DEPTH & (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1)) != 0)
#error "BM_CONFIG_ULTRA_QUEUE_DEPTH 须为 2 的幂且至少为 2"
#endif

#if BM_CONFIG_ULTRA_QUEUE_DEPTH > 256
#error "BM_CONFIG_ULTRA_QUEUE_DEPTH 使用 uint8_t 索引时不得超过 256"
#endif

#if BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE > 255
#error "BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE 不得超过 255"
#endif

#if BM_CPU_LOCAL_ENABLE_ROUTE

/** 每 CPU ultra 运行时状态（cache-line 对齐，防伪共享） */
typedef struct {
    bm_ultra_queue_t  q;
    uint32_t          dropped;
    uint32_t          dispatch_skipped;
} bm_ultra_cpu_state_t;

typedef struct {
    bm_ultra_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_ultra_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_ultra_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_ultra_cpu_storage_t g_ultra_cpu[BM_CONFIG_CPU_COUNT];

static bm_ultra_cpu_state_t *ultra_this(void) {
    uint32_t cpu = bm_hal_cpu_id();

    /* CPU 越界时返回 NULL，调用方统一 fail-closed */
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_ultra_cpu[cpu].state;
}

#define _ultra_q                (ultra_this()->q)
#define _ultra_dropped          (ultra_this()->dropped)
#define _ultra_dispatch_skipped (ultra_this()->dispatch_skipped)

/** @brief 校验 ultra 队列读写索引在合法掩码范围内 */
static int ultra_indices_valid(uint8_t read_idx, uint8_t write_idx) {
    uint8_t mask = (uint8_t)(BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u);
    return (read_idx <= mask && write_idx <= mask) ? BM_OK : BM_ERR_INVALID;
}

/**
 * @brief 向当前核的 ultra 队列推入一个事件
 *
 * @param item 事件项指针
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_ultra_queue_push(const bm_ultra_queue_item_t *item) {
    uint8_t next;
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }
    if (item->event_type >= BM_CONFIG_ULTRA_MAX_EVENT_TYPES) {
        return BM_ERR_INVALID;
    }
    if (item->data_len > BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE) {
        return BM_ERR_INVALID;
    }
    if (!ultra_this()) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(_ultra_q.read_idx, _ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (uint8_t)((_ultra_q.write_idx + 1u) &
                     (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    if (next == _ultra_q.read_idx) {
        _ultra_dropped = bm_u32_saturating_inc(_ultra_dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }
    _ultra_q.items[_ultra_q.write_idx] = *item;
    _ultra_q.write_idx = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

/**
 * @brief 从当前核的 ultra 队列弹出一个事件
 *
 * @param item 输出事件项指针
 * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 队列为空；负值表示其他失败
 */
int bm_ultra_queue_pop(bm_ultra_queue_item_t *item) {
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }
    if (!ultra_this()) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(_ultra_q.read_idx, _ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    if (_ultra_q.read_idx == _ultra_q.write_idx) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_WOULD_BLOCK;
    }
    *item = _ultra_q.items[_ultra_q.read_idx];
    _ultra_q.read_idx = (uint8_t)((_ultra_q.read_idx + 1u) &
                                   (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

void bm_ultra_queue_reset(void) {
    bm_ultra_cpu_state_t *st = ultra_this();
    bm_irq_state_t s;

    if (!st) {
        return;
    }
    s = BM_CRITICAL_ENTER();
    memset(&st->q, 0, sizeof(st->q));
    st->dropped = 0u;
    st->dispatch_skipped = 0u;
    BM_CRITICAL_EXIT(s);
}

/**
 * @brief 获取当前核 ultra 分发的跳过计数
 *
 * @return 跳过计数
 */
uint32_t bm_ultra_get_dispatch_skipped_count(void) {
    bm_ultra_cpu_state_t *st = ultra_this();
    bm_irq_state_t s;
    uint32_t skipped;

    if (!st) {
        return 0u;
    }
    s = BM_CRITICAL_ENTER();
    skipped = st->dispatch_skipped;
    BM_CRITICAL_EXIT(s);
    return skipped;
}

/**
 * @brief 获取当前核 ultra 队列的丢弃计数
 *
 * @return 丢弃计数
 */
uint32_t bm_ultra_get_dropped_count(void) {
    bm_ultra_cpu_state_t *st = ultra_this();
    bm_irq_state_t s;
    uint32_t dropped;

    if (!st) {
        return 0u;
    }
    s = BM_CRITICAL_ENTER();
    dropped = st->dropped;
    BM_CRITICAL_EXIT(s);
    return dropped;
}

/**
 * @brief 获取当前核 ultra 队列中待处理事件数
 *
 * @return 事件数量
 */
uint8_t bm_ultra_queue_count(void) {
    bm_ultra_cpu_state_t *st = ultra_this();
    bm_irq_state_t s;
    uint8_t count;

    if (!st) {
        return 0u;
    }
    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(st->q.read_idx, st->q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return 0u;
    }
    count = (uint8_t)(((uint32_t)st->q.write_idx -
                       (uint32_t)st->q.read_idx) &
                      (uint32_t)(BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    BM_CRITICAL_EXIT(s);
    return count;
}

/**
 * @brief 获取当前核 ultra 队列状态指针
 *
 * @return 队列状态指针；CPU 越界时返回 NULL
 */
const bm_ultra_queue_t *bm_ultra_queue_state(void) {
    bm_ultra_cpu_state_t *st = ultra_this();
    return st ? &st->q : NULL;
}

/**
 * @brief 处理当前核 ultra 队列中的一个事件
 *
 * @return 1 处理了一个事件；0 无事件或出错
 */
uint8_t bm_ultra_process(void) {
    bm_ultra_queue_item_t item;
    bm_ultra_callback_t cb;
    bm_ultra_cpu_state_t *st = ultra_this();
    int rc;

    if (!st) {
        return 0u;
    }

    rc = bm_ultra_queue_pop(&item);
    if (rc == BM_ERR_INVALID) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        st->dispatch_skipped =
            bm_u32_saturating_inc(st->dispatch_skipped);
        BM_CRITICAL_EXIT(s);
        return 0u;
    }
    if (rc != BM_OK) {
        return 0u;
    }
    if (item.event_type >= BM_CONFIG_ULTRA_MAX_EVENT_TYPES ||
        item.data_len > BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        st->dispatch_skipped =
            bm_u32_saturating_inc(st->dispatch_skipped);
        BM_CRITICAL_EXIT(s);
        return 1u;
    }
    cb = _bm_ultra_callbacks[item.event_type];
    if (cb != NULL) {
        cb(item.data, item.data_len);
    }
    return 1u;
}

#ifdef BM_ENABLE_ULTRA_TEST_HOOK
/**
 * @brief 测试钩子：直接向 ultra 队列注入事件
 *
 * @param item 事件项指针
 * @return BM_OK 成功；负值表示失败
 */
int bm_ultra_test_inject(const bm_ultra_queue_item_t *item) {
    uint8_t next;
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }
    if (!ultra_this()) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(_ultra_q.read_idx, _ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (uint8_t)((_ultra_q.write_idx + 1u) &
                     (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    if (next == _ultra_q.read_idx) {
        _ultra_dropped = bm_u32_saturating_inc(_ultra_dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }
    _ultra_q.items[_ultra_q.write_idx] = *item;
    _ultra_q.write_idx = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}
#endif /* BM_ENABLE_ULTRA_TEST_HOOK */

#else /* !BM_CPU_LOCAL_ENABLE_ROUTE — 默认路径 */

static bm_ultra_queue_t _bm_ultra_q;
static uint32_t         _bm_ultra_dropped;
static uint32_t         _bm_ultra_dispatch_skipped;

/** @brief 校验 ultra 队列读写索引在合法掩码范围内 */
static int ultra_indices_valid(uint8_t read_idx, uint8_t write_idx) {
    uint8_t mask = (uint8_t)(BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u);
    return (read_idx <= mask && write_idx <= mask) ? BM_OK : BM_ERR_INVALID;
}

/**
 * @brief 单核下向 ultra 队列推入一个事件
 *
 * @param item 事件项指针
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_ultra_queue_push(const bm_ultra_queue_item_t *item) {
    uint8_t next;
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }
    if (item->event_type >= BM_CONFIG_ULTRA_MAX_EVENT_TYPES) {
        return BM_ERR_INVALID;
    }
    if (item->data_len > BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(_bm_ultra_q.read_idx, _bm_ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (uint8_t)((_bm_ultra_q.write_idx + 1u) &
                     (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    if (next == _bm_ultra_q.read_idx) {
        _bm_ultra_dropped = bm_u32_saturating_inc(_bm_ultra_dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }
    _bm_ultra_q.items[_bm_ultra_q.write_idx] = *item;
    _bm_ultra_q.write_idx = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

/**
 * @brief 单核下从 ultra 队列弹出一个事件
 *
 * @param item 输出事件项指针
 * @return BM_OK 成功；BM_ERR_WOULD_BLOCK 队列为空；负值表示其他失败
 */
int bm_ultra_queue_pop(bm_ultra_queue_item_t *item) {
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(_bm_ultra_q.read_idx, _bm_ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    if (_bm_ultra_q.read_idx == _bm_ultra_q.write_idx) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_WOULD_BLOCK;
    }
    *item = _bm_ultra_q.items[_bm_ultra_q.read_idx];
    _bm_ultra_q.read_idx = (uint8_t)((_bm_ultra_q.read_idx + 1u) &
                                       (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

void bm_ultra_queue_reset(void) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    memset(&_bm_ultra_q, 0, sizeof(_bm_ultra_q));
    _bm_ultra_dropped = 0u;
    _bm_ultra_dispatch_skipped = 0u;
    BM_CRITICAL_EXIT(s);
}

/**
 * @brief 单核下获取 ultra 分发的跳过计数
 *
 * @return 跳过计数
 */
uint32_t bm_ultra_get_dispatch_skipped_count(void) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t skipped = _bm_ultra_dispatch_skipped;
    BM_CRITICAL_EXIT(s);
    return skipped;
}

/**
 * @brief 单核下获取 ultra 队列的丢弃计数
 *
 * @return 丢弃计数
 */
uint32_t bm_ultra_get_dropped_count(void) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t dropped = _bm_ultra_dropped;
    BM_CRITICAL_EXIT(s);
    return dropped;
}

/**
 * @brief 单核下获取 ultra 队列中待处理事件数
 *
 * @return 事件数量
 */
uint8_t bm_ultra_queue_count(void) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint8_t count;

    if (ultra_indices_valid(_bm_ultra_q.read_idx, _bm_ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return 0u;
    }
    count = (uint8_t)(((uint32_t)_bm_ultra_q.write_idx -
                       (uint32_t)_bm_ultra_q.read_idx) &
                      (uint32_t)(BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    BM_CRITICAL_EXIT(s);
    return count;
}

/**
 * @brief 单核下获取 ultra 队列状态指针
 *
 * @return 队列状态指针
 */
const bm_ultra_queue_t *bm_ultra_queue_state(void) {
    return &_bm_ultra_q;
}

/**
 * @brief 单核下处理 ultra 队列中的一个事件
 *
 * @return 1 处理了一个事件；0 无事件或出错
 */
uint8_t bm_ultra_process(void) {
    bm_ultra_queue_item_t item;
    bm_ultra_callback_t cb;
    int rc;

    rc = bm_ultra_queue_pop(&item);
    if (rc == BM_ERR_INVALID) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        _bm_ultra_dispatch_skipped =
            bm_u32_saturating_inc(_bm_ultra_dispatch_skipped);
        BM_CRITICAL_EXIT(s);
        return 0u;
    }
    if (rc != BM_OK) {
        return 0u;
    }
    if (item.event_type >= BM_CONFIG_ULTRA_MAX_EVENT_TYPES ||
        item.data_len > BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        _bm_ultra_dispatch_skipped =
            bm_u32_saturating_inc(_bm_ultra_dispatch_skipped);
        BM_CRITICAL_EXIT(s);
        return 1u;
    }
    cb = _bm_ultra_callbacks[item.event_type];
    if (cb != NULL) {
        cb(item.data, item.data_len);
    }
    return 1u;
}

#ifdef BM_ENABLE_ULTRA_TEST_HOOK
/**
 * @brief 测试钩子：直接向 ultra 队列注入事件
 *
 * @param item 事件项指针
 * @return BM_OK 成功；负值表示失败
 */
int bm_ultra_test_inject(const bm_ultra_queue_item_t *item) {
    uint8_t next;
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(_bm_ultra_q.read_idx, _bm_ultra_q.write_idx) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (uint8_t)((_bm_ultra_q.write_idx + 1u) &
                     (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    if (next == _bm_ultra_q.read_idx) {
        _bm_ultra_dropped = bm_u32_saturating_inc(_bm_ultra_dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }
    _bm_ultra_q.items[_bm_ultra_q.write_idx] = *item;
    _bm_ultra_q.write_idx = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}
#endif /* BM_ENABLE_ULTRA_TEST_HOOK */

#endif /* BM_CPU_LOCAL_ENABLE_ROUTE */

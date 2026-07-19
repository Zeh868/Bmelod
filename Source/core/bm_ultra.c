/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_ultra.c
 * @brief Ultra 剖面事件队列实现（按 CPU 分域）
 *
 * 默认路径下全局单例队列 + 计数器；按 CPU 路由时采用分域数组，各域独立。
 * 两条编译路径共享同一套队列逻辑，仅存储布局与 ultra_this() 的实现不同。
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            分域存储，路由启用时独立
 * 2026-07-02       1.2            zeh            两路径去重：统一 state 指针访问
 * 2026-07-02       1.3            zeh            QD-6：cache-line 补齐改用 union，
 *                                                消除 MSVC C2233
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

/** ultra 运行时状态：队列 + 丢弃/跳过计数（路由与默认路径共用同一结构） */
typedef struct {
    bm_ultra_queue_t  q;
    uint32_t          dropped;
    uint32_t          dispatch_skipped;
} bm_ultra_cpu_state_t;

#if BM_CPU_LOCAL_ENABLE_ROUTE

/** 每 CPU ultra 存储（cache-line 对齐，防伪共享） */
typedef BM_CACHE_LINE_PADDED_UNION(bm_ultra_cpu_state_t, state,
                                   BM_CONFIG_CACHE_LINE) bm_ultra_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_ultra_cpu_storage_t g_ultra_cpu[BM_CONFIG_CPU_COUNT];

/**
 * @brief 获取当前核 ultra 状态指针
 *
 * @return 状态指针；CPU 越界时返回 NULL（fail-closed）
 */
static bm_ultra_cpu_state_t *ultra_this(void) {
    uint32_t cpu = bm_hal_cpu_id();

    /* CPU 越界时返回 NULL，调用方统一 fail-closed */
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_ultra_cpu[cpu].state;
}

#else /* !BM_CPU_LOCAL_ENABLE_ROUTE — 默认路径：单一全局实例 */

static bm_ultra_cpu_state_t s_ultra_state;

/**
 * @brief 获取 ultra 状态指针（默认路径单例，恒非 NULL）
 *
 * @return 全局 ultra 状态指针
 */
static bm_ultra_cpu_state_t *ultra_this(void) {
    return &s_ultra_state;
}

#endif /* BM_CPU_LOCAL_ENABLE_ROUTE */

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
    bm_ultra_cpu_state_t *state = ultra_this();
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
    if (state == NULL) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(state->q.read_idx, state->q.write_idx) != BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (uint8_t)((state->q.write_idx + 1u) &
                     (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    if (next == state->q.read_idx) {
        state->dropped = bm_u32_saturating_inc(state->dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }
    state->q.items[state->q.write_idx] = *item;
    state->q.write_idx = next;
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
    bm_ultra_cpu_state_t *state = ultra_this();
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }
    if (state == NULL) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(state->q.read_idx, state->q.write_idx) != BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    if (state->q.read_idx == state->q.write_idx) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_WOULD_BLOCK;
    }
    *item = state->q.items[state->q.read_idx];
    state->q.read_idx = (uint8_t)((state->q.read_idx + 1u) &
                                  (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

/**
 * @brief 重置当前核的 ultra 队列与统计计数
 */
void bm_ultra_queue_reset(void) {
    bm_ultra_cpu_state_t *state = ultra_this();
    bm_irq_state_t s;

    if (state == NULL) {
        return;
    }
    s = BM_CRITICAL_ENTER();
    memset(&state->q, 0, sizeof(state->q));
    state->dropped = 0u;
    state->dispatch_skipped = 0u;
    BM_CRITICAL_EXIT(s);
}

/**
 * @brief 获取当前核 ultra 分发的跳过计数
 *
 * @return 跳过计数
 */
uint32_t bm_ultra_get_dispatch_skipped_count(void) {
    bm_ultra_cpu_state_t *state = ultra_this();
    bm_irq_state_t s;
    uint32_t skipped;

    if (state == NULL) {
        return 0u;
    }
    s = BM_CRITICAL_ENTER();
    skipped = state->dispatch_skipped;
    BM_CRITICAL_EXIT(s);
    return skipped;
}

/**
 * @brief 获取当前核 ultra 队列的丢弃计数
 *
 * @return 丢弃计数
 */
uint32_t bm_ultra_get_dropped_count(void) {
    bm_ultra_cpu_state_t *state = ultra_this();
    bm_irq_state_t s;
    uint32_t dropped;

    if (state == NULL) {
        return 0u;
    }
    s = BM_CRITICAL_ENTER();
    dropped = state->dropped;
    BM_CRITICAL_EXIT(s);
    return dropped;
}

/**
 * @brief 获取当前核 ultra 队列中待处理事件数
 *
 * @return 事件数量
 */
uint8_t bm_ultra_queue_count(void) {
    bm_ultra_cpu_state_t *state = ultra_this();
    bm_irq_state_t s;
    uint8_t count;

    if (state == NULL) {
        return 0u;
    }
    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(state->q.read_idx, state->q.write_idx) != BM_OK) {
        BM_CRITICAL_EXIT(s);
        return 0u;
    }
    count = (uint8_t)(((uint32_t)state->q.write_idx -
                       (uint32_t)state->q.read_idx) &
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
    bm_ultra_cpu_state_t *state = ultra_this();
    return state ? &state->q : NULL;
}

/**
 * @brief 处理当前核 ultra 队列中的一个事件
 *
 * @return 1 处理了一个事件；0 无事件或出错
 */
uint8_t bm_ultra_process(void) {
    bm_ultra_cpu_state_t *state = ultra_this();
    bm_ultra_queue_item_t item;
    bm_ultra_callback_t cb;
    int rc;

    if (state == NULL) {
        return 0u;
    }

    rc = bm_ultra_queue_pop(&item);
    if (rc == BM_ERR_INVALID) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        state->dispatch_skipped =
            bm_u32_saturating_inc(state->dispatch_skipped);
        BM_CRITICAL_EXIT(s);
        return 0u;
    }
    if (rc != BM_OK) {
        return 0u;
    }
    if (item.event_type >= BM_CONFIG_ULTRA_MAX_EVENT_TYPES ||
        item.data_len > BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE) {
        bm_irq_state_t s = BM_CRITICAL_ENTER();
        state->dispatch_skipped =
            bm_u32_saturating_inc(state->dispatch_skipped);
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
    bm_ultra_cpu_state_t *state = ultra_this();
    uint8_t next;
    bm_irq_state_t s;

    if (!item) {
        return BM_ERR_INVALID;
    }
    if (state == NULL) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (ultra_indices_valid(state->q.read_idx, state->q.write_idx) != BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (uint8_t)((state->q.write_idx + 1u) &
                     (BM_CONFIG_ULTRA_QUEUE_DEPTH - 1u));
    if (next == state->q.read_idx) {
        state->dropped = bm_u32_saturating_inc(state->dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }
    state->q.items[state->q.write_idx] = *item;
    state->q.write_idx = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}
#endif /* BM_ENABLE_ULTRA_TEST_HOOK */

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_event.c
 * @brief 发布-订阅事件总线实现
 *
 * 按优先级分 FIFO 队列 + 不可变链表订阅者；临界区保护多生产者/消费者。
 * 订阅表初始化后冻结，分发直接遍历链表——无快照，WCET 可预测。
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            订阅冻结，去快照/去 dispatch_active，
 *                                                确定性流式分发
 * 2026-06-15       1.2            zeh            默认禁用零拷贝发布并收紧冻结检查
 * 2026-06-15       1.3            zeh            MP hook 首次设置后不可替换
 * 2026-06-15       1.4            zeh            零拷贝禁用守卫提前，消除 ISR 死代码路径
 *
 */
#include "bm/core/bm_cpu_local.h"

#include "bm_event.h"
#include "bm_critical_wrap.h"
#include "bm_log.h"
#include "bm_safety.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifndef BM_CONFIG_EVENT_PRIORITY_BURST_MAX
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX 8
#endif

#ifndef BM_CONFIG_MAX_EVENT_SUBSCRIBERS_PER_TYPE
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS_PER_TYPE 4
#endif

#ifndef BM_CONFIG_EVENT_ENABLE_ZERO_COPY_PUBLISH
#define BM_CONFIG_EVENT_ENABLE_ZERO_COPY_PUBLISH 0
#endif

#if BM_CONFIG_EVENT_PRIORITY_BURST_MAX < 1
#error "BM_CONFIG_EVENT_PRIORITY_BURST_MAX 至少为 1"
#endif

#if BM_CONFIG_EVENT_QUEUE_SIZE < BM_CONFIG_EVENT_PRIORITIES || \
    (BM_CONFIG_EVENT_QUEUE_SIZE % BM_CONFIG_EVENT_PRIORITIES) != 0
#error "BM_CONFIG_EVENT_QUEUE_SIZE 须能被 BM_CONFIG_EVENT_PRIORITIES 整除"
#endif

#define BM_EVENT_QUEUE_DEPTH_PER_PRIO \
    (BM_CONFIG_EVENT_QUEUE_SIZE / BM_CONFIG_EVENT_PRIORITIES)

#if BM_EVENT_QUEUE_DEPTH_PER_PRIO < 2 || \
    ((BM_EVENT_QUEUE_DEPTH_PER_PRIO & (BM_EVENT_QUEUE_DEPTH_PER_PRIO - 1)) != 0)
#error "BM_EVENT_QUEUE_DEPTH_PER_PRIO 须为 2 的幂且至少为 2"
#endif

#if defined(_MSC_VER)
#define BM_EVENT_ALIGNAS(bytes) __declspec(align(bytes))
#elif defined(__GNUC__) || defined(__clang__)
#define BM_EVENT_ALIGNAS(bytes) __attribute__((aligned(bytes)))
#else
#error "bm_event requires compiler support for explicit data alignment"
#endif

/** 订阅者节点（冻结后链表不可变，分发可免锁遍历） */
typedef struct bm_subscriber {
    bm_event_callback_t           cb;
    void                         *user_data;
    bm_event_subscriber_id_t      id;
    struct bm_subscriber         *next;
} bm_subscriber_t;

/** 事件类型槽：名称 + 订阅者链表头 */
typedef struct {
    const char          *name;
    bm_subscriber_t     *head;
} bm_event_type_slot_t;

/** 队列项：事件 + 内联数据缓冲 */
typedef struct {
    bm_event_t  event;
    BM_EVENT_ALIGNAS(8) uint8_t
        inline_data[BM_CONFIG_EVENT_INLINE_DATA_SIZE];
} bm_queue_item_t;

#undef BM_EVENT_ALIGNAS

/** 按 CPU 分域的事件子系统运行时状态 */
typedef struct {
    bm_event_type_slot_t     event_types[BM_CONFIG_MAX_EVENT_TYPES];
    bm_subscriber_t          subscribers[BM_CONFIG_MAX_EVENT_SUBSCRIBERS];
    bm_queue_item_t          prio_items[BM_CONFIG_EVENT_PRIORITIES]
                             [BM_EVENT_QUEUE_DEPTH_PER_PRIO];
    uint32_t                 prio_read[BM_CONFIG_EVENT_PRIORITIES];
    uint32_t                 prio_write[BM_CONFIG_EVENT_PRIORITIES];
    uint32_t                 next_subscriber_id;
    uint32_t                 queue_dropped;
    uint32_t                 dispatch_skipped;
    uint32_t                 reentrancy_rejected;
    uint32_t                 dispatch_depth;
    uint32_t                 events_since_fair;
    uint32_t                 fair_prio_cursor;
    bool                     subscriptions_frozen;
} bm_event_cpu_state_t;

typedef struct {
    bm_event_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_event_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_event_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_event_cpu_storage_t g_event_cpu[BM_CONFIG_CPU_COUNT];
static bm_event_owner_resolver_t s_owner_resolver;
static bm_event_forwarder_t s_forwarder;

void bm_event_set_route_hooks(bm_event_owner_resolver_t owner_resolver,
                              bm_event_forwarder_t forwarder) {
    if ((s_owner_resolver != NULL || s_forwarder != NULL) &&
        (s_owner_resolver != owner_resolver || s_forwarder != forwarder)) {
        return;
    }
    s_owner_resolver = owner_resolver;
    s_forwarder = forwarder;
}

/**
 * @brief 若事件所属核不是当前核，则通过转发钩子递送
 *
 * @param event 事件指针
 * @param data 载荷数据指针
 * @param len 载荷长度
 * @param forwarded 输出参数：是否已转发
 * @return BM_OK 可本地入队或转发成功；负值表示转发失败
 */
static int event_forward_if_remote(const bm_event_t *event,
                                   const void *data, size_t len,
                                   int *forwarded) {
    uint8_t owner;

    *forwarded = 0;
    if (!s_owner_resolver || !s_forwarder) {
        return BM_OK;
    }
    owner = s_owner_resolver(event->type);
    if (owner == BM_CPU_ANY) {
        return BM_ERR_NOT_INIT;
    }
    if (owner == (uint8_t)BM_CPU_THIS()) {
        return BM_OK;
    }
    *forwarded = 1;
    return s_forwarder(owner, event, data, len);
}

/**
 * @brief 获取当前核事件状态
 *
 * @return 状态指针；CPU 越界时返回 NULL（fail-closed）
 */
static bm_event_cpu_state_t *bm_event_this(void) {
    uint32_t cpu = BM_CPU_THIS();

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_event_cpu[cpu].state;
}

static int event_require_cpu(void) {
    if (bm_event_this() == NULL) {
        BM_LOGE("event", "invalid cpu %u", (unsigned)BM_CPU_THIS());
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

#define _event_types        (bm_event_this()->event_types)
#define _subscribers        (bm_event_this()->subscribers)
#define _prio_items         (bm_event_this()->prio_items)
#define _prio_read          (bm_event_this()->prio_read)
#define _prio_write         (bm_event_this()->prio_write)
#define _next_subscriber_id (bm_event_this()->next_subscriber_id)
#define _queue_dropped      (bm_event_this()->queue_dropped)
#define _dispatch_skipped   (bm_event_this()->dispatch_skipped)
#define _reentrancy_rejected (bm_event_this()->reentrancy_rejected)
#define _dispatch_depth     (bm_event_this()->dispatch_depth)
#define _events_since_fair  (bm_event_this()->events_since_fair)
#define _fair_prio_cursor   (bm_event_this()->fair_prio_cursor)
#define _subscriptions_frozen (bm_event_this()->subscriptions_frozen)

/**
 * @brief 复制队列项，正确处理内联数据指针自引用
 *
 * 源 item 的 event.data 可能指向自己的 inline_data；拷贝后需将 dst->event.data
 * 重定向到 dst 自身的 inline_data 缓冲区。
 */
static void _queue_item_copy(bm_queue_item_t *dst, const bm_queue_item_t *src) {
    bool owns_inline_data = (src->event.data == src->inline_data);

    *dst = *src;
    if (owns_inline_data) {
        dst->event.data = dst->inline_data;
    }
}

/** @brief 校验队列项字段合法（fail-stop） */
static int _queue_item_valid(const bm_queue_item_t *item) {
    if (item->event.type >= BM_CONFIG_MAX_EVENT_TYPES ||
        item->event.priority >= BM_CONFIG_EVENT_PRIORITIES ||
        item->event.data_len > BM_CONFIG_EVENT_INLINE_DATA_SIZE) {
        return BM_ERR_INVALID;
    }
    if (item->event.data_len == 0u) {
        return item->event.data == NULL ? BM_OK : BM_ERR_INVALID;
    }
    return item->event.data == item->inline_data ? BM_OK : BM_ERR_INVALID;
}

static void _prio_queues_reset(void) {
    memset(_prio_items, 0, sizeof(_prio_items));
    memset(_prio_read, 0, sizeof(_prio_read));
    memset(_prio_write, 0, sizeof(_prio_write));
}

/** 校验优先级队列读写索引在槽位掩码范围内（fail-stop） */
static int _prio_indices_valid(uint32_t read_idx, uint32_t write_idx,
                               uint32_t mask) {
    return (read_idx <= mask && write_idx <= mask) ? BM_OK : BM_ERR_INVALID;
}

/**
 * @brief 冻结事件订阅表，禁止后续注册/订阅变更
 *
 * 冻结前会审计已注册但无订阅者的事件类型并打印 warning。
 */
void bm_event_freeze_subscriptions(void) {
    bm_irq_state_t s;
    uint32_t i;
    uint32_t unbound_count = 0u;

    if (event_require_cpu() != BM_OK) {
        return;
    }
    s = BM_CRITICAL_ENTER();
    /*
     * 静态接线审计：冻结时检查已注册但无订阅者的事件类型。
     * 非 hard RT 剖面仅 warning；hard RT 由 partition_build 前置 fail-closed。
     */
    for (i = 0u; i < BM_CONFIG_MAX_EVENT_TYPES; i++) {
        if (_event_types[i].name != NULL && _event_types[i].head == NULL) {
            unbound_count =
                bm_u32_saturating_inc(unbound_count);
        }
    }
    _subscriptions_frozen = true;
    BM_CRITICAL_EXIT(s);
    if (unbound_count > 0u) {
        BM_LOGW("event", "%u registered types have no subscribers",
                (unsigned)unbound_count);
    }
    BM_LOGD("event", "subscriptions frozen for deterministic dispatch");
}

/**
 * @brief 重置当前核的事件总线状态
 *
 * 清空事件类型、订阅者、优先级队列与统计计数器。
 */
void bm_event_reset(void) {
    bm_irq_state_t s;

    if (event_require_cpu() != BM_OK) {
        return;
    }
    s = BM_CRITICAL_ENTER();
    if (_dispatch_depth > 0u) {
        _reentrancy_rejected =
            bm_u32_saturating_inc(_reentrancy_rejected);
        BM_CRITICAL_EXIT(s);
        return;
    }
    memset(_event_types, 0, sizeof(_event_types));
    memset(_subscribers, 0, sizeof(_subscribers));
    _prio_queues_reset();
    _next_subscriber_id = 1;
    _queue_dropped = 0;
    _dispatch_skipped = 0;
    _reentrancy_rejected = 0;
    _dispatch_depth = 0u;
    _events_since_fair = 0u;
    _fair_prio_cursor = (BM_CONFIG_EVENT_PRIORITIES > 1) ? 1u : 0u;
    _subscriptions_frozen = false;
    BM_CRITICAL_EXIT(s);
    BM_LOGI("event", "event bus reset");
}

/**
 * @brief 注册一个事件类型
 *
 * @param type 事件类型枚举值
 * @param name 事件名称字符串
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_BUSY 冻结或分发中；
 *         BM_ERR_ALREADY 该类型已注册
 */
int bm_event_register_type(bm_event_type_t type, const char *name) {
    if (event_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (type >= BM_CONFIG_MAX_EVENT_TYPES || !name) {
        BM_LOGE("event", "register_type invalid type=%u", (unsigned)type);
        return BM_ERR_INVALID;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    /*
     * 冻结后禁止注册新事件类型，保证分区器 event→owner 表不变。
     */
    if (_subscriptions_frozen || _dispatch_depth > 0u) {
        _reentrancy_rejected =
            bm_u32_saturating_inc(_reentrancy_rejected);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_BUSY;
    }
    if (_event_types[type].name != NULL) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("event", "type %u already registered", (unsigned)type);
        return BM_ERR_ALREADY;
    }
    _event_types[type].name = name;
    BM_CRITICAL_EXIT(s);
    BM_LOGD("event", "type %u registered as '%s'", (unsigned)type, name);
    return BM_OK;
}

/**
 * @brief 检查订阅变更是否允许
 *
 * 冻结后或在分发回调内调用 subscribe/unsubscribe 会拒绝。
 */
static int event_subscription_change_allowed(void) {
    if (_subscriptions_frozen || _dispatch_depth > 0u) {
        _reentrancy_rejected =
            bm_u32_saturating_inc(_reentrancy_rejected);
        return BM_ERR_BUSY;
    }
    return BM_OK;
}

/**
 * @brief 订阅指定类型事件
 *
 * @param type 事件类型
 * @param cb 事件回调函数
 * @param user_data 回调用户数据
 * @param id 输出参数：订阅者 ID（可为 NULL）
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_event_subscribe(bm_event_type_t type, bm_event_callback_t cb,
                       void *user_data, bm_event_subscriber_id_t *id) {
    if (event_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (!cb || type >= BM_CONFIG_MAX_EVENT_TYPES) {
        BM_LOGE("event", "subscribe invalid args type=%u", (unsigned)type);
        return BM_ERR_INVALID;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();

    bm_subscriber_t *sub = NULL;
    int i;
    int allowed = event_subscription_change_allowed();

    if (allowed != BM_OK) {
        BM_CRITICAL_EXIT(s);
        return allowed;
    }
    if (_event_types[type].name == NULL) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    /*
     * 静态 fan-out 上界：每事件类型订阅者数 ≤ 编译期常量，
     * 保证分发时间为 O(1) 且 WCET 可预测。
     */
    {
        bm_subscriber_t *sub_iter = _event_types[type].head;
        uint32_t type_sub_count = 0u;

        while (sub_iter) {
            type_sub_count++;
            sub_iter = sub_iter->next;
        }
        if (type_sub_count >= BM_CONFIG_MAX_EVENT_SUBSCRIBERS_PER_TYPE) {
            BM_CRITICAL_EXIT(s);
            BM_LOGW("event", "subscribe type=%u fan-out bound %u exceeded",
                    (unsigned)type, (unsigned)BM_CONFIG_MAX_EVENT_SUBSCRIBERS_PER_TYPE);
            return BM_ERR_NO_MEM;
        }
    }
#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (s_owner_resolver) {
        uint8_t owner = s_owner_resolver(type);

        if (owner == BM_CPU_ANY) {
            BM_CRITICAL_EXIT(s);
            return BM_ERR_NOT_INIT;
        }
        if (owner != (uint8_t)BM_CPU_THIS()) {
            BM_CRITICAL_EXIT(s);
            return BM_ERR_INVALID;
        }
    }
#endif
    for (i = 0; i < BM_CONFIG_MAX_EVENT_SUBSCRIBERS; i++) {
        if (_subscribers[i].id == 0) {
            sub = &_subscribers[i];
            break;
        }
    }
    if (!sub) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("event", "subscribe no free slot for type=%u", (unsigned)type);
        return BM_ERR_NO_MEM;
    }
    if (_next_subscriber_id == 0u) {
        BM_CRITICAL_EXIT(s);
        BM_LOGW("event", "subscribe id exhausted");
        return BM_ERR_NO_MEM;
    }

    sub->cb = cb;
    sub->user_data = user_data;
    sub->id = _next_subscriber_id++;
    sub->next = _event_types[type].head;
    _event_types[type].head = sub;
    if (id) {
        *id = sub->id;
    }

    BM_CRITICAL_EXIT(s);
    BM_LOGD("event", "subscribed id=%u type=%u", (unsigned)sub->id, (unsigned)type);
    return BM_OK;
}

/**
 * @brief 取消订阅指定类型事件
 *
 * @param type 事件类型
 * @param id 订阅者 ID
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_event_unsubscribe(bm_event_type_t type, bm_event_subscriber_id_t id) {
    if (event_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (type >= BM_CONFIG_MAX_EVENT_TYPES || id == 0) {
        return BM_ERR_INVALID;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();

    int allowed = event_subscription_change_allowed();

    if (allowed != BM_OK) {
        BM_CRITICAL_EXIT(s);
        return allowed;
    }
    if (_event_types[type].name == NULL) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    bm_subscriber_t **pp = &_event_types[type].head;
    while (*pp) {
        if ((*pp)->id == id) {
            bm_subscriber_t *to_remove = *pp;

            *pp = to_remove->next;
            to_remove->id = 0;
            to_remove->cb = NULL;
            to_remove->next = NULL;
            BM_CRITICAL_EXIT(s);
            BM_LOGD("event", "unsubscribed id=%u type=%u", (unsigned)id, (unsigned)type);
            return BM_OK;
        }
        pp = &(*pp)->next;
    }

    BM_CRITICAL_EXIT(s);
    BM_LOGW("event", "unsubscribe id=%u not found", (unsigned)id);
    return BM_ERR_NOT_FOUND;
}

static int _prio_push_copy(bm_event_priority_t prio, const bm_event_t *event,
                           const void *data, size_t len) {
    /*
     * 每优先级独立环形队列，深度为 2 的幂以便用掩码代替取模，
     * 缩短临界区内的索引运算时间。
     */
    uint32_t mask = BM_EVENT_QUEUE_DEPTH_PER_PRIO - 1u;
    uint32_t next;
    bm_queue_item_t *item;
    bm_irq_state_t s;

    if (prio >= BM_CONFIG_EVENT_PRIORITIES) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (_prio_indices_valid(_prio_read[prio], _prio_write[prio], mask) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        BM_LOGE("event", "push corrupt indices prio=%u", (unsigned)prio);
        return BM_ERR_INVALID;
    }
    next = (_prio_write[prio] + 1u) & mask;
    if (next == _prio_read[prio]) {
        _queue_dropped = bm_u32_saturating_inc(_queue_dropped);
        BM_CRITICAL_EXIT(s);
        BM_LOGW("event", "queue overflow type=%u prio=%u",
                (unsigned)event->type, (unsigned)prio);
        return BM_ERR_OVERFLOW;
    }

    if (data && len > sizeof(_prio_items[prio][0].inline_data)) {
        BM_CRITICAL_EXIT(s);
        BM_LOGE("event", "payload too large len=%u", (unsigned)len);
        return BM_ERR_NO_MEM;
    }

    item = &_prio_items[prio][_prio_write[prio] & mask];
    item->event = *event;

    if (data && len > 0u) {
        memcpy(item->inline_data, data, len);
        item->event.data = item->inline_data;
        item->event.data_len = (uint8_t)len;
    } else {
        item->event.data = NULL;
        item->event.data_len = 0;
    }

    _prio_write[prio] = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

/**
 * @brief 通用的发布实现：校验 → 路由转发 → 入队
 *
 * 冻结后无 _dispatch_active 检查；从回调发布事件链是确定性的正常操作。
 */
static int event_publish_impl(bm_event_type_t type, bm_event_priority_t prio,
                              const bm_event_t *event_template,
                              const void *data, size_t len,
                              bool from_isr,
                              bool preserve_source) {
    bm_event_t ev;
    bm_irq_state_t s;
    int forwarded;
    int rc;

    /*
     * 确定性流式 ISR 安全契约（事件/ultra/mempool 软实时队列通用）：
     *
     * - 非掩码模式（BM_CONFIG_ENABLE_PRIORITY_MASK=0，默认）：
     *   BM_CRITICAL_ENTER() 全关中断，任何 ISR 与主循环路径互斥，*_from_isr
     *   变体安全。
     * - 掩码模式（=1）：BM_CRITICAL_ENTER() 仅屏蔽低于 HRT 阈值的中断。因此：
     *   · 低于 HRT 阈值的普通 ISR 调用 *_from_isr 仍被正确互斥——这是预期用法。
     *   · **HRT 级（>= 阈值）ISR 不得调用 event/ultra/mempool API**。这些是软
     *     实时队列；HRT 级路径只应经 HRT binding（直驱硬件）与 stream/relay
     *     通道交互。刻意不在此处升级为全关中断——否则 HRT ISR 在队列操作期间
     *     被阻塞，反而增大 HRT 抖动，违背确定性流式对最高优先级路径的保证。
     * - 单核路径：每核独立事件队列，转发钩子按注册规则路由；ISR 内 publish
     *   无需额外自旋路径。
     */
    (void)from_isr;

    if (event_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (type >= BM_CONFIG_MAX_EVENT_TYPES ||
        prio >= BM_CONFIG_EVENT_PRIORITIES ||
        (len > 0u && !data)) {
        return BM_ERR_INVALID;
    }

    if (event_template) {
        ev = *event_template;
    } else {
        ev.type = type;
        ev.priority = prio;
        ev.data = NULL;
        ev.data_len = (uint8_t)len;
    }
    if (!preserve_source) {
        ev.source_id = (uint8_t)BM_CPU_THIS();
    }

    rc = event_forward_if_remote(&ev, data, len, &forwarded);
    if (forwarded || rc != BM_OK) {
        return rc;
    }

    s = BM_CRITICAL_ENTER();
    if (_event_types[type].name == NULL) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_NOT_INIT;
    }
    BM_CRITICAL_EXIT(s);

    if (event_template) {
        return _prio_push_copy(prio, &ev,
                               event_template->data, event_template->data_len);
    }
    return _prio_push_copy(prio, &ev, data, len);
}

/**
 * @brief 以拷贝方式发布事件（主循环路径）
 *
 * @param type 事件类型
 * @param prio 事件优先级
 * @param data 载荷数据指针
 * @param len 载荷长度
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_event_publish_copy(bm_event_type_t type, bm_event_priority_t prio,
                          const void *data, size_t len) {
    return event_publish_impl(type, prio, NULL, data, len, false, false);
}

/**
 * @brief 以拷贝方式发布事件并指定源 CPU
 *
 * @param type 事件类型
 * @param prio 事件优先级
 * @param source_id 源 CPU ID
 * @param data 载荷数据指针
 * @param len 载荷长度
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_event_publish_copy_from_source(bm_event_type_t type,
                                      bm_event_priority_t prio,
                                      uint8_t source_id,
                                      const void *data,
                                      size_t len) {
    bm_event_t event;

    if (source_id >= BM_CONFIG_CPU_COUNT) {
        return BM_ERR_INVALID;
    }
    event.type = type;
    event.priority = prio;
    event.source_id = source_id;
    event.data = data;
    event.data_len = (uint8_t)len;
    return event_publish_impl(type, prio, &event, data, len, false, true);
}

/**
 * @brief 以拷贝方式发布事件（ISR 路径）
 *
 * @param type 事件类型
 * @param prio 事件优先级
 * @param data 载荷数据指针
 * @param len 载荷长度
 * @return BM_OK 成功；负值表示失败原因
 */
int bm_event_publish_copy_from_isr(bm_event_type_t type, bm_event_priority_t prio,
                                   const void *data, size_t len) {
    return event_publish_impl(type, prio, NULL, data, len, true, false);
}

/**
 * @brief 零拷贝发布事件（默认禁用）
 *
 * @param event 完整事件描述符指针
 * @return BM_OK 成功；BM_ERR_NOT_SUPPORTED 默认未启用零拷贝；负值表示其他失败
 */
int bm_event_publish_event(const bm_event_t *event) {
    if (!event ||
        event->type >= BM_CONFIG_MAX_EVENT_TYPES ||
        event->priority >= BM_CONFIG_EVENT_PRIORITIES) {
        return BM_ERR_INVALID;
    }
    if (event->data_len > 0u && !event->data) {
        return BM_ERR_INVALID;
    }
    /*
     * 确定性流式剖面禁止零拷贝 publish_event：
     * 调用方保证 data 存活到 process 结束，WCET 与所有权不可静态分析。
     * 流式路径仅允许 publish_copy / publish_copy_from_isr。
     */
#if !BM_CONFIG_EVENT_ENABLE_ZERO_COPY_PUBLISH
    (void)event;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (event->data_len > BM_CONFIG_EVENT_INLINE_DATA_SIZE) {
        return BM_ERR_NO_MEM;
    }
#if BM_CONFIG_HARD_RT_PROFILE
    BM_LOGW("event", "publish_event rejected in hard-RT profile:"
            " zero-copy data lifetime not statically analyzable;"
            " use publish_copy with inline data instead type=%u",
            (unsigned)event->type);
    (void)event;
    return BM_ERR_NOT_SUPPORTED;
#else
    return event_publish_impl(event->type, event->priority, event,
                              event->data, event->data_len, false, false);
#endif
#endif
}

/**
 * @brief 零拷贝发布事件（ISR 路径，默认禁用）
 *
 * @param event 完整事件描述符指针
 * @return BM_OK 成功；BM_ERR_NOT_SUPPORTED 默认未启用零拷贝；负值表示其他失败
 */
int bm_event_publish_event_from_isr(const bm_event_t *event) {
    if (!event ||
        event->type >= BM_CONFIG_MAX_EVENT_TYPES ||
        event->priority >= BM_CONFIG_EVENT_PRIORITIES ||
        (event->data_len > 0u && !event->data)) {
        return BM_ERR_INVALID;
    }
#if !BM_CONFIG_EVENT_ENABLE_ZERO_COPY_PUBLISH
    (void)event;
    return BM_ERR_NOT_SUPPORTED;
#else
    if (event->data_len > BM_CONFIG_EVENT_INLINE_DATA_SIZE) {
        return BM_ERR_NO_MEM;
    }
#if BM_CONFIG_HARD_RT_PROFILE
    BM_LOGW("event", "publish_event_from_isr rejected in hard-RT profile:"
            " zero-copy data lifetime not statically analyzable in ISR;"
            " use publish_copy_from_isr with inline data instead type=%u",
            (unsigned)event->type);
    (void)event;
    return BM_ERR_NOT_SUPPORTED;
#else
    return event_publish_impl(event->type, event->priority, event,
                              event->data, event->data_len, true, false);
#endif
#endif
}

static int _queue_pop_highest_prio(bm_queue_item_t *out) {
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t prio;
    uint32_t selected = BM_CONFIG_EVENT_PRIORITIES;
    uint32_t mask = BM_EVENT_QUEUE_DEPTH_PER_PRIO - 1u;
    bool lower_prio_pending = false;

    for (prio = 0u; prio < BM_CONFIG_EVENT_PRIORITIES; ++prio) {
        if (_prio_indices_valid(_prio_read[prio], _prio_write[prio], mask) !=
            BM_OK) {
            BM_CRITICAL_EXIT(s);
            BM_LOGE("event", "pop corrupt indices prio=%u", (unsigned)prio);
            return BM_ERR_INVALID;
        }
        if (selected == BM_CONFIG_EVENT_PRIORITIES &&
            _prio_read[prio] != _prio_write[prio]) {
            selected = prio;
        }
    }

    if (selected == BM_CONFIG_EVENT_PRIORITIES) {
        _events_since_fair = 0u;
        BM_CRITICAL_EXIT(s);
        return BM_ERR_WOULD_BLOCK;
    }

    for (prio = selected + 1u; prio < BM_CONFIG_EVENT_PRIORITIES; ++prio) {
        if (_prio_read[prio] != _prio_write[prio]) {
            lower_prio_pending = true;
            break;
        }
    }

    if (lower_prio_pending &&
        _events_since_fair >= BM_CONFIG_EVENT_PRIORITY_BURST_MAX) {
        /*
         * 公平性轮转：从 _fair_prio_cursor 出发，循环扫描寻找
         * 优先级低于 selected（数值更大）的非空队列。
         * 使用模运算回绕扫描，candidate > selected 确保只选更低优先级。
         */
        for (prio = 0u; prio < BM_CONFIG_EVENT_PRIORITIES; ++prio) {
            uint32_t candidate =
                (_fair_prio_cursor + prio) % BM_CONFIG_EVENT_PRIORITIES;

            if (candidate > selected &&
                _prio_read[candidate] != _prio_write[candidate]) {
                selected = candidate;
                break;
            }
        }
        _fair_prio_cursor = (selected + 1u) % BM_CONFIG_EVENT_PRIORITIES;
        _events_since_fair = 0u;
    } else if (lower_prio_pending) {
        _events_since_fair =
            bm_u32_saturating_inc(_events_since_fair);
    } else {
        _events_since_fair = 0u;
    }

    {
        uint32_t slot = _prio_read[selected] & mask;

        _queue_item_copy(out, &_prio_items[selected][slot]);
        _prio_read[selected] = (_prio_read[selected] + 1u) & mask;
    }
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}

/**
 * @brief 获取当前核因队列满而丢弃的事件数
 *
 * @return 丢弃计数
 */
uint32_t bm_event_get_dropped_count(void) {
    if (event_require_cpu() != BM_OK) {
        return 0u;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t dropped = _queue_dropped;
    BM_CRITICAL_EXIT(s);
    return dropped;
}

/**
 * @brief 获取当前核因队列损坏而跳过分发的事件数
 *
 * @return 跳过计数
 */
uint32_t bm_event_get_dispatch_skipped_count(void) {
    if (event_require_cpu() != BM_OK) {
        return 0u;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t skipped = _dispatch_skipped;
    BM_CRITICAL_EXIT(s);
    return skipped;
}

/**
 * @brief 获取当前核因重入或冻结而被拒绝的操作数
 *
 * @return 拒绝计数
 */
uint32_t bm_event_get_reentrancy_rejected_count(void) {
    if (event_require_cpu() != BM_OK) {
        return 0u;
    }
    bm_irq_state_t s = BM_CRITICAL_ENTER();
    uint32_t rejected = _reentrancy_rejected;
    BM_CRITICAL_EXIT(s);
    return rejected;
}

/**
 * @brief 分发最多 max_events 个事件给订阅者
 *
 * @param max_events 本轮最大处理事件数
 * @return 实际处理事件数；负值表示错误
 */
int bm_event_process(uint32_t max_events) {
    uint32_t processed = 0u;
    uint32_t i;
    bm_irq_state_t entry_state;

    if (event_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }

    /*
     * 确定性流式防护：订阅表冻结后才允许分发。
     * 冻结前调用 process 可能遍历正在修改的链表，导致悬空指针。
     */
    if (!_subscriptions_frozen) {
        BM_LOGW("event", "process blocked: subscriptions not frozen");
        return BM_ERR_NOT_INIT;
    }

    /*
     * 非可重入防护：仅主循环调用 process。
     * 回调中再次调用 process 返回 BM_ERR_BUSY。
     */
    entry_state = BM_CRITICAL_ENTER();
    if (_dispatch_depth > 0u) {
        _reentrancy_rejected =
            bm_u32_saturating_inc(_reentrancy_rejected);
        BM_CRITICAL_EXIT(entry_state);
        return BM_ERR_BUSY;
    }
    _dispatch_depth++;
    BM_CRITICAL_EXIT(entry_state);

    for (i = 0u; i < max_events; i++) {
        bm_queue_item_t item;
        int rc = _queue_pop_highest_prio(&item);

        if (rc == BM_ERR_INVALID) {
            bm_irq_state_t s = BM_CRITICAL_ENTER();
            _dispatch_skipped =
                bm_u32_saturating_inc(_dispatch_skipped);
            BM_CRITICAL_EXIT(s);
            BM_LOGE("event", "process corrupt queue");
            break;
        }
        if (rc != BM_OK) {
            break;
        }

        if (_queue_item_valid(&item) != BM_OK) {
            BM_LOGE("event", "process invalid queued event type=%u len=%u",
                    (unsigned)item.event.type,
                    (unsigned)item.event.data_len);
            bm_irq_state_t s = BM_CRITICAL_ENTER();
            _dispatch_skipped =
                bm_u32_saturating_inc(_dispatch_skipped);
            BM_CRITICAL_EXIT(s);
            processed++;
            continue;
        }

        /*
         * 冻结后订阅链表不可变：直接在临界区外遍历分发，
         * 无需快照。回调安全地 publish 新事件入队，不会修改链表。
         */
        {
            bm_subscriber_t *sub;

            /* 在临界区内读链表头（单指针读，冻结后安全） */
            bm_irq_state_t s = BM_CRITICAL_ENTER();
            sub = _event_types[item.event.type].head;
            BM_CRITICAL_EXIT(s);

            while (sub) {
                if (sub->cb) {
                    sub->cb(&item.event, sub->user_data);
                }
                sub = sub->next;
            }
        }
        processed++;
    }

    entry_state = BM_CRITICAL_ENTER();
    _dispatch_depth--;
    BM_CRITICAL_EXIT(entry_state);

    BM_LOGT("event", "processed %u events", (unsigned)processed);
    return (int)processed;
}

#ifdef BM_ENABLE_EVENT_TEST_HOOK
/**
 * @brief 测试钩子：直接注入事件到指定优先级队列
 *
 * @param event 事件描述符指针
 * @param prio 目标优先级
 * @return BM_OK 成功；负值表示失败
 */
int bm_event_test_inject(const bm_event_t *event, bm_event_priority_t prio) {
    uint32_t mask = BM_EVENT_QUEUE_DEPTH_PER_PRIO - 1u;
    uint32_t next;
    bm_queue_item_t *item;
    bm_irq_state_t s;

    if (event_require_cpu() != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (!event || prio >= BM_CONFIG_EVENT_PRIORITIES) {
        return BM_ERR_INVALID;
    }

    s = BM_CRITICAL_ENTER();
    if (_prio_indices_valid(_prio_read[prio], _prio_write[prio], mask) !=
        BM_OK) {
        BM_CRITICAL_EXIT(s);
        return BM_ERR_INVALID;
    }
    next = (_prio_write[prio] + 1u) & mask;
    if (next == _prio_read[prio]) {
        _queue_dropped = bm_u32_saturating_inc(_queue_dropped);
        BM_CRITICAL_EXIT(s);
        return BM_ERR_OVERFLOW;
    }

    item = &_prio_items[prio][_prio_write[prio] & mask];
    memset(item, 0, sizeof(*item));
    item->event = *event;
    if (event->data && event->data_len <= sizeof(item->inline_data)) {
        memcpy(item->inline_data, event->data, event->data_len);
        item->event.data = item->inline_data;
    }
    _prio_write[prio] = next;
    BM_CRITICAL_EXIT(s);
    return BM_OK;
}
#endif /* BM_ENABLE_EVENT_TEST_HOOK */

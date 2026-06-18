/**
 * @file bm_ticker.c
 * @brief 毫秒级周期事件发布器实现
 *
 * 主循环轮询到期槽，向事件总线发布空载荷事件；统计丢弃次数。
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-11       1.1            zeh            SIL-2 队列满时推进周期
 * 2026-06-11       1.2            zeh            init 校验 event 索引与定时器频率
 * 2026-06-11       1.3            zeh            get_dropped 临界区读取
 * 2026-06-14       1.4            zeh            分域：每 CPU 独立 ticker 槽表
 * 2026-06-15       1.5            zeh            poll 热路径移除格式化日志
 *
 */
#include "bm_ticker.h"
#include "bm_critical_wrap.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_hal_timer.h"
#include "bm_log.h"
#include "bm_safety.h"
#include "bm_time.h"
#include "hal/bm_hal_cpu.h"

#include <string.h>

/** 运行时槽：公开描述 + 周期与丢弃计数 */
typedef struct {
    bm_ticker_slot_t pub;
    uint32_t period_ticks;
    uint32_t next_tick;
    uint32_t dropped;
} bm_ticker_runtime_slot_t;

/** 按 CPU 分域的 ticker 运行时状态 */
typedef struct {
    bm_ticker_runtime_slot_t slots[BM_CONFIG_TICKER_MAX_SLOTS];
    uint32_t                   slot_count;
    int                        initialized;
} bm_ticker_cpu_state_t;

typedef struct {
    bm_ticker_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_ticker_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_ticker_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_ticker_cpu_storage_t g_ticker_cpu[BM_CONFIG_CPU_COUNT];

/**
 * @brief 获取当前核 ticker 状态指针
 */
static bm_ticker_cpu_state_t *bm_ticker_this(void) {
    uint32_t cpu = bm_hal_cpu_id();

    /* CPU 越界时返回 NULL，调用方统一 fail-closed */
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_ticker_cpu[cpu].state;
}

/**
 * @brief 使用 uint32_t 模加法计算下次 ticker 触发时刻
 */
static uint32_t ticker_deadline_from(uint32_t base, uint32_t period) {
    return base + period;
}

/**
 * @brief 初始化毫秒级周期事件发布器
 *
 * @param slots 槽描述符数组
 * @param slot_count 槽数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 槽数超限
 */
int bm_ticker_init(const bm_ticker_slot_t *slots, uint32_t slot_count) {
    bm_ticker_cpu_state_t *state = bm_ticker_this();
    bm_irq_state_t irq_state;
    uint32_t i;
    uint32_t now;

    if (!state) {
        return BM_ERR_INVALID;
    }

    if (state->initialized) {
        BM_LOGW("ticker", "init while active");
        return BM_ERR_ALREADY;
    }

    if (!slots && slot_count > 0u) {
        BM_LOGE("ticker", "init invalid slots");
        return BM_ERR_INVALID;
    }
    if (slot_count > BM_CONFIG_TICKER_MAX_SLOTS) {
        BM_LOGE("ticker", "init overflow count=%u", (unsigned)slot_count);
        return BM_ERR_OVERFLOW;
    }

    for (i = 0u; i < slot_count; ++i) {
        if (slots[i].period_ms == 0u) {
            BM_LOGE("ticker", "init slot %u zero period", (unsigned)i);
            return BM_ERR_INVALID;
        }
        if (slots[i].event_type >= BM_CONFIG_MAX_EVENT_TYPES) {
            BM_LOGE("ticker", "init slot %u invalid event type", (unsigned)i);
            return BM_ERR_INVALID;
        }
        if (slots[i].priority >= BM_CONFIG_EVENT_PRIORITIES) {
            BM_LOGE("ticker", "init slot %u invalid priority", (unsigned)i);
            return BM_ERR_INVALID;
        }
    }

    if (bm_hal_timer_get_freq() == 0u) {
        BM_LOGE("ticker", "init timer not configured");
        return BM_ERR_NOT_INIT;
    }

    /*
     * 确定性流式安全：整个 slot 填充在临界区内完成。
     * bm_ticker_get_dropped 可来自 ISR 上下文——在 slot 填充
     * 与 initialized 置位之间若存在窗口，ISR 可能读到半初始化数据。
     */
    irq_state = BM_CRITICAL_ENTER();
    memset(state->slots, 0, sizeof(state->slots));
    state->slot_count = 0u;
    now = bm_hal_timer_get_ticks();

    {
        /*
         * 失败信息延迟到临界区外再记录：日志格式化不应延长关中断时长。
         * err_slot/err_conv 仅在临界区内捕获，退出后据此输出对应日志。
         */
        int rc = BM_OK;
        uint32_t err_slot = 0u;
        int err_conv = 0;

        for (i = 0u; i < slot_count; ++i) {
            uint32_t period_ticks = 0u;
            int trc = bm_time_ms_to_ticks(slots[i].period_ms, &period_ticks);
            if (trc != BM_OK || period_ticks > (uint32_t)INT32_MAX) {
                memset(state->slots, 0, sizeof(state->slots));
                state->slot_count = 0u;
                state->initialized = 0;
                rc = BM_ERR_INVALID;
                err_slot = i;
                err_conv = (trc != BM_OK) ? 1 : 0;
                break;
            }
            state->slots[i].pub = slots[i];
            state->slots[i].period_ticks = period_ticks;
            state->slots[i].next_tick = ticker_deadline_from(now, period_ticks);
        }

        if (rc == BM_OK) {
            state->slot_count = slot_count;
            state->initialized = 1;
        }
        BM_CRITICAL_EXIT(irq_state);

        if (rc != BM_OK) {
            if (err_conv) {
                BM_LOGE("ticker", "init slot %u tick conversion failed",
                        (unsigned)err_slot);
            } else {
                BM_LOGE("ticker", "init slot %u period exceeds timer range",
                        (unsigned)err_slot);
            }
            return rc;
        }
    }

    BM_LOGI("ticker", "init %u slots", (unsigned)slot_count);
    return BM_OK;
}

/**
 * @brief 轮询到期槽并向事件总线发布事件
 *
 * 非可重入，仅限主循环调用。
 *
 * @return 本次发布的事件数；BM_ERR_NOT_INIT 未初始化
 */
int bm_ticker_poll(void) {
    bm_ticker_cpu_state_t *state = bm_ticker_this();
    uint32_t now;
    uint32_t i;
    int published = 0;

    if (!state || !state->initialized) {
        return BM_ERR_NOT_INIT;
    }

    now = bm_hal_timer_get_ticks();

    for (i = 0u; i < state->slot_count; ++i) {
        bm_ticker_runtime_slot_t *slot = &state->slots[i];

        {
            uint32_t catchup = 0u;
            while (bm_time_reached_u32(now, slot->next_tick) &&
                   catchup < BM_CONFIG_TICKER_MAX_CATCHUP) {
                int rc = bm_event_publish_copy(slot->pub.event_type,
                                               slot->pub.priority,
                                               NULL, 0u);
                if (rc == BM_ERR_OVERFLOW) {
                    slot->dropped = bm_u32_saturating_inc(slot->dropped);
                    slot->next_tick = ticker_deadline_from(
                        slot->next_tick, slot->period_ticks);
                    break;
                }
                if (rc != BM_OK) {
                    return rc;
                }
                published++;
                catchup++;
                slot->next_tick = ticker_deadline_from(
                    slot->next_tick, slot->period_ticks);
            }
            if (bm_time_reached_u32(now, slot->next_tick)) {
                slot->next_tick =
                    ticker_deadline_from(now, slot->period_ticks);
            }
        }
    }

    return published;
}

/**
 * @brief 查询指定槽因队列满而丢弃的事件次数
 *
 * @param slot_index 槽索引
 * @return 丢弃次数；索引无效返回 0
 */
uint32_t bm_ticker_get_dropped(uint32_t slot_index) {
    bm_ticker_cpu_state_t *state = bm_ticker_this();
    bm_irq_state_t irq_state;
    uint32_t count = 0u;

    if (!state) {
        return 0u;
    }
    irq_state = BM_CRITICAL_ENTER();
    if (bm_index_in_range_u32(slot_index, state->slot_count)) {
        count = state->slots[slot_index].dropped;
    }
    BM_CRITICAL_EXIT(irq_state);
    return count;
}

/**
 * @brief 重置发布器全部内部状态
 */
void bm_ticker_reset(void) {
    bm_ticker_cpu_state_t *state = bm_ticker_this();
    bm_irq_state_t irq_state;

    if (!state) {
        return;
    }
    irq_state = BM_CRITICAL_ENTER();
    state->initialized = 0;
    state->slot_count = 0u;
    memset(state->slots, 0, sizeof(state->slots));
    BM_CRITICAL_EXIT(irq_state);
    BM_LOGI("ticker", "reset");
}

/**
 * @brief 查询 ticker 是否已初始化
 *
 * @return 非零已初始化；0 未初始化或无效 CPU
 */
int bm_ticker_is_initialized(void) {
    bm_ticker_cpu_state_t *state = bm_ticker_this();
    bm_irq_state_t irq_state;
    int initialized;

    if (!state) {
        return 0;
    }
    irq_state = BM_CRITICAL_ENTER();
    initialized = state->initialized;
    BM_CRITICAL_EXIT(irq_state);
    return initialized;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hrt.c
 * @brief 高分辨率定时（HRT）调度器实现
 *
 * 基于 HAL 定时器 ISR 按周期触发回调；支持 deadline 错过弱钩子。
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-11       1.1            zeh            SIL-2 临界区、tick 算术与 miss 计数
 * 2026-06-11       1.2            zeh            全槽 miss 合计与可配置默认钩子日志
 * 2026-06-14       1.3            zeh            分域：每 CPU 独立槽表与 IRQ release 门控
 * 2026-06-15       1.4            zeh            start/stop/reset 临界区移除日志
 * 2026-06-15       1.5            zeh            协作式 bm_hrt_poll 供 QEMU 慢仿真
 *
 */
#include "bm_hrt.h"
#include "bm_config.h"
#include "bm/common/bm_atomic_ipc.h"
#include "bm/core/bm_cpu_local.h"
#include "bm/core/bm_cpu_local.h"
#include "bm_critical_wrap.h"
#include "bm_hal_timer.h"
#include "bm_log.h"
#include "bm_safety.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_critical.h"

#include <string.h>

#if BM_CONFIG_HRT_TICK_US == 0u || (1000000u % BM_CONFIG_HRT_TICK_US) != 0u
#error "BM_CONFIG_HRT_TICK_US 须非零且能整除 1000000"
#endif

#ifndef BM_CONFIG_HRT_RUN_MISSED_CALLBACK
#define BM_CONFIG_HRT_RUN_MISSED_CALLBACK 0
#endif

/** 运行时槽：公开描述 + 周期 tick 与下次触发时刻 */
typedef struct {
    bm_hrt_slot_t pub;
    uint32_t period_ticks;
    uint32_t next_tick;
    uint32_t deadline_missed;
} bm_hrt_runtime_slot_t;

/** 按 CPU 分域的 HRT 调度器运行时状态 */
typedef struct {
    bm_hrt_runtime_slot_t slots[BM_CONFIG_HRT_MAX_SLOTS];
    uint32_t              slot_count;
    int                   initialized;
    int                   started;
} bm_hrt_cpu_state_t;

typedef struct {
    bm_hrt_cpu_state_t state;
    uint8_t padding[BM_CONFIG_CACHE_LINE -
                    (sizeof(bm_hrt_cpu_state_t) % BM_CONFIG_CACHE_LINE)];
} bm_hrt_cpu_storage_t;

static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE)
bm_hrt_cpu_storage_t g_hrt_cpu[BM_CONFIG_CPU_COUNT];
static bm_hrt_start_gate_t s_start_gate;

/**
 * @brief 获取当前核 HRT 状态指针
 */
static bm_hrt_cpu_state_t *bm_hrt_this(void) {
    uint32_t cpu = bm_hal_cpu_id();
    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return NULL;
    }
    return &g_hrt_cpu[cpu].state;
}

#define g_slots       (bm_hrt_this()->slots)
#define g_slot_count  (bm_hrt_this()->slot_count)
#define g_initialized (bm_hrt_this()->initialized)
#define g_started     (bm_hrt_this()->started)

/** @brief 检查启动门控是否允许启动 HRT */
static int hrt_require_irq_released(void) {
    return s_start_gate ? s_start_gate() : BM_OK;
}

/** @brief 检查当前 CPU 的 HRT 状态是否有效 */
static int hrt_cpu_valid(void) {
    return bm_hrt_this() != NULL;
}

/**
 * @brief 设置 HRT 启动门控回调
 *
 * 在 bm_hrt_start 调用前检查是否允许启动定时器。
 *
 * @param gate 门控回调函数指针
 */
void bm_hrt_set_start_gate(bm_hrt_start_gate_t gate) {
    s_start_gate = gate;
}

#if !defined(BM_CONFIG_HRT_EXTERNAL_DEADLINE_HOOK) || \
    !(BM_CONFIG_HRT_EXTERNAL_DEADLINE_HOOK)
/* IAR/ARMCC/MSVC 不识别 weak：须设 BM_CONFIG_HRT_EXTERNAL_DEADLINE_HOOK=1 由应用提供钩子 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
/**
 * @brief HRT 槽错过 deadline 时的弱钩子（默认空实现）
 *
 * @param slot 触发 deadline 错过的槽描述符
 */
void bm_hrt_deadline_missed_hook(const bm_hrt_slot_t *slot) {
    (void)slot;
}
#endif

/**
 * @brief 根据配置计算 HRT 定时器频率（Hz）
 *
 * @return 定时器 tick 频率
 */
static uint32_t hrt_tick_hz(void) {
    return 1000000u / BM_CONFIG_HRT_TICK_US;
}

/**
 * @brief 使用 uint32_t 模加法计算下次触发 tick
 */
static uint32_t hrt_deadline_from(uint32_t base, uint32_t period) {
    return base + period;
}

/**
 * @brief 在已持锁或 ISR 内停止定时器（不进出临界区）
 *
 * 确定性流式安全：停止硬件定时器后执行全屏障（DSB+ISB），
 * 确保已 latch 的 ISR 执行完毕，防止回调置 NULL 后 ISR 仍访问
 * 已被释放的槽位数据（use-after-free）。
 */
static void hrt_stop_locked(void) {
    if (!hrt_cpu_valid()) {
        return;
    }
    if (!g_started) {
        return;
    }
    bm_hal_timer_stop();
    /*
     * 全屏障保证：1) 硬件定时器停止操作已完成；
     * 2) 已 latch 的 pending ISR 执行完毕。
     * ARM 上为 DSB + ISB，x86 上为 mfence + 序列化指令。
     */
    bm_atomic_ipc_fence_full();
    bm_hal_timer_set_callback(NULL);
    g_started = 0;
}

/**
 * @brief 扫描所有槽并触发到期回调
 */
static void hrt_dispatch(void) {
    uint32_t now = bm_hal_timer_get_ticks();
    uint32_t i;
    uint32_t fired = 0u;

    if (!hrt_cpu_valid()) {
        return;
    }
    /*
     * 遍历槽位并分派就绪回调，受 BM_CONFIG_HRT_DISPATCH_PER_ISR 限制；
     * 各回调 WCET 须相对 BM_CONFIG_HRT_TICK_US 预算，以保证 ISR
     * 总时长有界且确定。
     */
    for (i = 0u; i < g_slot_count &&
         fired < BM_CONFIG_HRT_DISPATCH_PER_ISR; ++i) {
        bm_hrt_runtime_slot_t *slot = &g_slots[i];

        if (slot->pub.trigger != BM_HRT_TRIGGER_TIMER) {
            continue;
        }
        if (slot->period_ticks == 0u) {
            continue;
        }
        if (!bm_time_reached_u32(now, slot->next_tick)) {
            continue;
        }
        if ((uint32_t)(now - slot->next_tick) >= slot->period_ticks) {
            slot->deadline_missed =
                bm_u32_saturating_inc(slot->deadline_missed);
            bm_hrt_deadline_missed_hook(&slot->pub);
#if BM_CONFIG_HRT_RUN_MISSED_CALLBACK
            if (slot->pub.callback) {
                slot->pub.callback(slot->pub.context);
                fired++;
            }
#endif
            slot->next_tick = hrt_deadline_from(now, slot->period_ticks);
            continue;
        }
        if (slot->pub.callback) {
            slot->pub.callback(slot->pub.context);
            fired++;
        }
        slot->next_tick =
            hrt_deadline_from(slot->next_tick, slot->period_ticks);
    }
}

/**
 * @brief HAL 定时器 ISR 入口，分派 HRT 回调
 */
/**
 * @brief HAL 定时器 ISR 入口，分派 HRT 回调
 */
static void hrt_timer_isr(void) {
    hrt_dispatch();
}

/**
 * @brief 协作式轮询 HRT（用于 QEMU 慢仿真等无精确中断场景）
 */
void bm_hrt_poll(void) {
    if (bm_hal_in_isr()) {
        return;
    }
    if (!hrt_cpu_valid() || !g_started) {
        return;
    }
    hrt_dispatch();
}

int bm_hrt_validate_period_us(uint32_t period_us) {
    uint32_t period_ticks;

    if (period_us == 0u ||
        (period_us % BM_CONFIG_HRT_TICK_US) != 0u) {
        return BM_ERR_INVALID;
    }
    period_ticks = period_us / BM_CONFIG_HRT_TICK_US;
    if (period_ticks == 0u || period_ticks > (uint32_t)INT32_MAX) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/** @brief 校验单个 HRT 槽描述符 */
static int validate_slot(const bm_hrt_slot_t *slot) {
    if (!slot || !slot->callback) {
        return BM_ERR_INVALID;
    }
    if (slot->trigger != BM_HRT_TRIGGER_TIMER) {
        return BM_ERR_INVALID;
    }
    return bm_hrt_validate_period_us(slot->period_us);
}

/**
 * @brief 初始化 HRT 调度器槽表
 *
 * @param slots 槽描述符数组
 * @param slot_count 槽数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_OVERFLOW 槽数超限
 */
int bm_hrt_init(const bm_hrt_slot_t *slots, uint32_t slot_count) {
    bm_irq_state_t irq_state;
    uint32_t i;
    uint32_t now;
    int rc;

    /*
     * 入参校验只读调用方数组与本核常量状态，无需临界区保护；放在临界区外
     * 完成，使失败路径的日志（snprintf + ring 推送）不延长关中断时长，
     * 满足确定性流式对临界区时长可预测的要求。
     */
    if (!hrt_cpu_valid()) {
        return BM_ERR_INVALID;
    }
    if (!slots && slot_count > 0u) {
        BM_LOGE("hrt", "init invalid slots pointer");
        return BM_ERR_INVALID;
    }
    if (slot_count > BM_CONFIG_HRT_MAX_SLOTS) {
        BM_LOGE("hrt", "init slot overflow count=%u", (unsigned)slot_count);
        return BM_ERR_OVERFLOW;
    }
    for (i = 0u; i < slot_count; ++i) {
        rc = validate_slot(&slots[i]);
        if (rc != BM_OK) {
            BM_LOGE("hrt", "init slot %u validation failed", (unsigned)i);
            return rc;
        }
    }

    irq_state = BM_CRITICAL_ENTER();
    if (g_started) {
        rc = BM_ERR_ALREADY;
        goto out;
    }
    memset(g_slots, 0, sizeof(g_slots));
    g_slot_count = slot_count;
    now = bm_hal_timer_get_ticks();
    for (i = 0u; i < slot_count; ++i) {
        g_slots[i].pub = slots[i];
        g_slots[i].period_ticks = slots[i].period_us / BM_CONFIG_HRT_TICK_US;
        g_slots[i].next_tick =
            hrt_deadline_from(now, g_slots[i].period_ticks);
    }
    g_initialized = 1;
    rc = BM_OK;

out:
    BM_CRITICAL_EXIT(irq_state);
    if (rc == BM_ERR_ALREADY) {
        BM_LOGE("hrt", "init while started");
    } else if (rc == BM_OK) {
        BM_LOGI("hrt", "init %u slots tick_us=%u", (unsigned)slot_count,
                (unsigned)BM_CONFIG_HRT_TICK_US);
    }
    return rc;
}

/**
 * @brief 启动 HRT 定时器并注册 ISR 回调
 *
 * @return BM_OK 成功；BM_ERR_ALREADY 已启动；BM_ERR_NOT_INIT 未初始化；
 *         其他为 HAL 初始化错误码
 */
int bm_hrt_start(void) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    int rc = BM_OK;

    if (!hrt_cpu_valid()) {
        rc = BM_ERR_INVALID;
        goto out;
    }
    if (g_started) {
        rc = BM_ERR_ALREADY;
        goto out;
    }
    if (!g_initialized) {
        rc = BM_ERR_NOT_INIT;
        goto out;
    }
    if (g_slot_count == 0u) {
        goto out;
    }

    BM_CRITICAL_EXIT(irq_state);

    rc = hrt_require_irq_released();
    if (rc != BM_OK) {
        BM_LOGE("hrt", "start blocked: irq not released");
        return BM_ERR_NOT_INIT;
    }

    bm_hal_timer_set_callback(NULL);
    rc = bm_hal_timer_init(hrt_tick_hz());
    if (rc != BM_OK) {
        BM_LOGE("hrt", "hal timer init failed rc=%d", rc);
        return rc;
    }

    irq_state = BM_CRITICAL_ENTER();
    if (g_started || !g_initialized) {
        if (g_started) {
            bm_hal_timer_stop();
            bm_hal_timer_set_callback(NULL);
        }
        BM_CRITICAL_EXIT(irq_state);
        BM_LOGW("hrt", "start race: started=%d init=%d",
                (int)g_started, (int)g_initialized);
        return g_started ? BM_ERR_ALREADY : BM_ERR_NOT_INIT;
    }

    bm_hal_timer_set_callback(hrt_timer_isr);

    {
        uint32_t now = bm_hal_timer_get_ticks();
        uint32_t i;

        for (i = 0u; i < g_slot_count; ++i) {
            g_slots[i].next_tick =
                hrt_deadline_from(now, g_slots[i].period_ticks);
        }
    }

    g_started = 1;

out:
    BM_CRITICAL_EXIT(irq_state);
    if (rc == BM_ERR_ALREADY) {
        BM_LOGW("hrt", "already started");
    } else if (rc == BM_ERR_NOT_INIT) {
        BM_LOGE("hrt", "start before init");
    } else if (rc == BM_OK) {
        BM_LOGI("hrt", "start completed");
    }
    return rc;
}

/**
 * @brief 停止 HRT 定时器并注销 ISR 回调
 */
void bm_hrt_stop(void) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    int stopped = 0;

    if (hrt_cpu_valid() && g_started) {
        hrt_stop_locked();
        stopped = 1;
    }
    BM_CRITICAL_EXIT(irq_state);
    if (stopped) {
        BM_LOGI("hrt", "stopped");
    }
}

/**
 * @brief 停止调度器并清空全部槽位状态
 */
void bm_hrt_reset(void) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    int reset = 0;

    if (!hrt_cpu_valid()) {
        BM_CRITICAL_EXIT(irq_state);
        return;
    }
    hrt_stop_locked();
    memset(g_slots, 0, sizeof(g_slots));
    g_slot_count = 0u;
    g_initialized = 0;
    reset = 1;
    BM_CRITICAL_EXIT(irq_state);
    if (reset) {
        BM_LOGI("hrt", "reset");
    }
}

/**
 * @brief 获取指定 HRT 槽的 deadline miss 次数
 *
 * @param slot_index 槽索引
 * @return miss 次数；索引无效返回 0
 */
uint32_t bm_hrt_get_deadline_missed(uint32_t slot_index) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    uint32_t count = 0u;

    if (hrt_cpu_valid() && bm_index_in_range_u32(slot_index, g_slot_count)) {
        count = g_slots[slot_index].deadline_missed;
    }
    BM_CRITICAL_EXIT(irq_state);
    return count;
}

/**
 * @brief 获取所有 HRT 槽的 deadline miss 总次数
 *
 * @return 总 miss 次数
 */
uint32_t bm_hrt_get_deadline_missed_total(void) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    uint32_t total = 0u;
    uint32_t i;

    if (!hrt_cpu_valid()) {
        BM_CRITICAL_EXIT(irq_state);
        return 0u;
    }
    for (i = 0u; i < g_slot_count; ++i) {
        total = bm_u32_saturating_add(
            total, g_slots[i].deadline_missed);
    }
    BM_CRITICAL_EXIT(irq_state);
    return total;
}

/**
 * @brief 查询 HRT 调度器是否已启动
 *
 * @return 非零已启动；0 未启动或无效 CPU
 */
int bm_hrt_is_started(void) {
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
    int started = hrt_cpu_valid() ? g_started : 0;

    BM_CRITICAL_EXIT(irq_state);
    return started;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_hrtimer_native.c
 * @brief native_sim 高精度 Timer 后端
 *
 * 以 `bm_uptime_us()` 为时间基，纯软件模拟高精度 Timer 行为。
 * 支持周期/单次/Output Compare、动态改比较值、deadline miss 统计。
 * 测试可通过 `bm_hal_hrtimer_native_advance_us()` 推进虚拟时间并触发回调。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim 高精度 Timer 后端
 */
#include "bm_drv_hrtimer.h"
#include "hal/bm_hal_hrtimer.h"
#include "bm_hal_hrtimer_native.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_uptime.h"

#include <stdint.h>
#include <string.h>

/** @brief 支持的 Timer 实例数。 */
#define BM_NATIVE_HRTIMER_COUNT 2u

/** @brief 默认计数频率（Hz）：1MHz，分辨率 1µs。 */
#define BM_NATIVE_HRTIMER_FREQ_HZ 1000000u

/** @brief 最大支持周期（µs）：约 1 小时（避免 64 位溢出）。 */
#define BM_NATIVE_HRTIMER_MAX_PERIOD_US 3600000000u

/** @brief 最小支持周期（µs）。 */
#define BM_NATIVE_HRTIMER_MIN_PERIOD_US 1u

/** @brief 单个 Timer 实例运行时状态。 */
typedef struct {
    const struct bm_hal_hrtimer *dev;    /**< 所属设备实例 */
    uint32_t                     mode;   /**< 运行模式 */
    uint32_t                     period_us;     /**< 当前周期/超时 */
    uint64_t                     next_expire_us;/**< 下次到期时刻 */
    bm_hrtimer_callback_t        callback;      /**< 到期回调 */
    void                        *user;          /**< 回调透传参数 */
    int                          running;       /**< 运行中标志 */
    bm_hrtimer_stats_t           stats;         /**< 统计信息 */
} bm_native_hrtimer_state_t;

static bm_native_hrtimer_state_t s_states[BM_NATIVE_HRTIMER_COUNT];
static uint64_t s_now_offset_us; /**< 测试用时间偏移量 */

/**
 * @brief 读取当前虚拟时间（µs）。
 */
static uint64_t bm_native_hrtimer_now_us(void) {
    return bm_uptime_us() + s_now_offset_us;
}

/**
 * @brief 由设备实例索引获取状态；无效时返回 NULL。
 */
static bm_native_hrtimer_state_t *bm_native_hrtimer_state_for(
    const struct bm_hal_hrtimer *dev) {
    if (dev == &bm_native_hrtimer0) {
        s_states[0].dev = dev;
        return &s_states[0];
    }
    if (dev == &bm_native_hrtimer1) {
        s_states[1].dev = dev;
        return &s_states[1];
    }
    return NULL;
}

/* -------------------------------------------------------------------------- */
/*  测试辅助接口                                                                */
/* -------------------------------------------------------------------------- */

uint64_t bm_hal_hrtimer_native_now_us(void) {
    return bm_native_hrtimer_now_us();
}

void bm_hal_hrtimer_native_reset(void) {
    (void)memset(s_states, 0, sizeof(s_states));
    s_now_offset_us = 0u;
}

/**
 * @brief 触发单个 Timer 的到期处理。
 *
 * 内部函数；供 advance_us 与外部测试钩共用。
 */
static void bm_native_hrtimer_fire_one(bm_native_hrtimer_state_t *state,
                                       uint64_t now) {
    if (state->running == 0 || state->period_us == 0u) {
        return;
    }

    while (now >= state->next_expire_us) {
        state->stats.irq_count++;
        if (now > state->next_expire_us + BM_NATIVE_HRTIMER_MIN_PERIOD_US) {
            state->stats.deadline_miss_count++;
        }

        if (state->callback != NULL) {
            state->callback(state->dev, state->user);
        }

        if (state->mode == BM_HRTIMER_MODE_PERIODIC) {
            state->next_expire_us += state->period_us;
        } else {
            state->running = 0;
            break;
        }
    }
}

void bm_hal_hrtimer_native_fire(const bm_hal_hrtimer_t *dev) {
    bm_native_hrtimer_state_t *state;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return;
    }
    bm_native_hrtimer_fire_one(state, bm_native_hrtimer_now_us());
}

void bm_hal_hrtimer_native_advance_us(uint64_t delta_us) {
    uint64_t now;
    uint32_t i;

    s_now_offset_us += delta_us;
    now = bm_native_hrtimer_now_us();

    for (i = 0u; i < BM_NATIVE_HRTIMER_COUNT; ++i) {
        bm_native_hrtimer_fire_one(&s_states[i], now);
    }
}

/* -------------------------------------------------------------------------- */
/*  driver API 实现                                                             */
/* -------------------------------------------------------------------------- */

static int native_hrtimer_init(const struct bm_hal_hrtimer *dev, void *config) {
    bm_native_hrtimer_state_t *state;

    (void)config;
    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->running = 0;
    state->mode = BM_HRTIMER_MODE_PERIODIC;
    state->period_us = 0u;
    state->next_expire_us = 0u;
    state->callback = NULL;
    state->user = NULL;
    (void)memset(&state->stats, 0, sizeof(state->stats));
    return BM_OK;
}

static int native_hrtimer_start(const struct bm_hal_hrtimer *dev,
                                uint32_t mode, uint32_t period_us) {
    bm_native_hrtimer_state_t *state;
    uint64_t now;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (mode != BM_HRTIMER_MODE_PERIODIC && mode != BM_HRTIMER_MODE_ONESHOT) {
        return BM_ERR_INVALID;
    }
    if (period_us < BM_NATIVE_HRTIMER_MIN_PERIOD_US
        || period_us > BM_NATIVE_HRTIMER_MAX_PERIOD_US) {
        return BM_ERR_INVALID;
    }

    now = bm_native_hrtimer_now_us();
    state->mode = mode;
    state->period_us = period_us;
    state->next_expire_us = now + period_us;
    state->running = 1;
    return BM_OK;
}

static int native_hrtimer_stop(const struct bm_hal_hrtimer *dev) {
    bm_native_hrtimer_state_t *state;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->running = 0;
    return BM_OK;
}

static int native_hrtimer_set_compare(const struct bm_hal_hrtimer *dev,
                                      uint32_t compare_us) {
    bm_native_hrtimer_state_t *state;
    uint64_t now;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    if (compare_us < BM_NATIVE_HRTIMER_MIN_PERIOD_US
        || compare_us > BM_NATIVE_HRTIMER_MAX_PERIOD_US) {
        return BM_ERR_INVALID;
    }

    now = bm_native_hrtimer_now_us();
    state->mode = BM_HRTIMER_MODE_ONESHOT;
    state->period_us = compare_us;
    state->next_expire_us = now + compare_us;
    /* set_compare 隐含运行 */
    state->running = 1;
    return BM_OK;
}

static uint32_t native_hrtimer_get_freq(const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return BM_NATIVE_HRTIMER_FREQ_HZ;
}

static uint32_t native_hrtimer_get_resolution_ns(
    const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return 1000u; /* 1MHz => 1000ns/tick */
}

static uint32_t native_hrtimer_get_max_period_us(
    const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return BM_NATIVE_HRTIMER_MAX_PERIOD_US;
}

static uint32_t native_hrtimer_get_min_period_us(
    const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return BM_NATIVE_HRTIMER_MIN_PERIOD_US;
}

static int native_hrtimer_get_stats(const struct bm_hal_hrtimer *dev,
                                    bm_hrtimer_stats_t *stats) {
    bm_native_hrtimer_state_t *state;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL || stats == NULL) {
        return BM_ERR_INVALID;
    }
    *stats = state->stats;
    return BM_OK;
}

static int native_hrtimer_set_callback(const struct bm_hal_hrtimer *dev,
                                       bm_hrtimer_callback_t cb, void *user) {
    bm_native_hrtimer_state_t *state;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->callback = cb;
    state->user = user;
    return BM_OK;
}

static const struct bm_hrtimer_driver_api g_native_hrtimer_api = {
    native_hrtimer_init,
    native_hrtimer_start,
    native_hrtimer_stop,
    native_hrtimer_set_compare,
    native_hrtimer_get_freq,
    native_hrtimer_get_resolution_ns,
    native_hrtimer_get_max_period_us,
    native_hrtimer_get_min_period_us,
    native_hrtimer_get_stats,
    native_hrtimer_set_callback,
};

const bm_hal_hrtimer_t bm_native_hrtimer0 = { &g_native_hrtimer_api, NULL };
const bm_hal_hrtimer_t bm_native_hrtimer1 = { &g_native_hrtimer_api, NULL };

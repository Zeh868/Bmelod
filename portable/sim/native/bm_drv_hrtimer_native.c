/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_hrtimer_native.c
 * @brief native_sim 高精度 Timer 后端
 * @maturity E1
 *
 * 以后端内部纯虚拟计数为时间基（不读墙钟），纯软件模拟高精度 Timer 行为。
 * 支持周期/单次/Output Compare、动态改比较值、deadline miss 统计。
 * 测试可通过 `bm_hal_hrtimer_native_advance_us()` 推进虚拟时间并触发回调。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim 高精度 Timer 后端
 * 2026-07-28       1.1            zeh            ONESHOT 回调内重武装不再被覆盖清除
 * 2026-07-28       1.2            zeh            PERIODIC 回调内 stop/重武装被尊重；
 *                                             大跨度 advance 有界合并；set_compare
 *                                             保留当前模式与运行状态；改纯虚拟时间基
 * 2026-07-31       1.3            zeh            回调派发首尾成对调用
 *                                             bm_hrt_isr_enter/exit，与真实 Hardware
 *                                             HRT 端口一致，消除"仿真放行、真机拒绝"分叉
 * 2026-08-01       1.3            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_drv_hrtimer.h"
#include "hal/bm_hal_hrtimer.h"
#include "bm_hal_hrtimer_native.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_critical_wrap.h"

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
static uint64_t s_virtual_now_us; /**< 后端内部纯虚拟时间（µs），不读墙钟 */

/**
 * @brief 读取当前虚拟时间（µs）。
 */
static uint64_t bm_native_hrtimer_now_us(void) {
    return s_virtual_now_us;
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
    s_virtual_now_us = 0u;
}

/** @brief 单次 advance 最多派发的回调次数；超出部分计入 deadline miss 并算术推进。 */
#define BM_NATIVE_HRTIMER_MAX_FIRE_PER_ADVANCE 100000u

/**
 * @brief 触发单个 Timer 的到期处理。
 *
 * 内部函数；供 advance_us 与外部测试钩共用。
 * 无回调的周期定时用算术合并错过的周期，避免大跨度 advance 逐次循环；
 * 有回调时单次 advance 派发次数受 BM_NATIVE_HRTIMER_MAX_FIRE_PER_ADVANCE 限制。
 */
static void bm_native_hrtimer_fire_one(bm_native_hrtimer_state_t *state,
                                       uint64_t now) {
    uint32_t fired = 0u;

    if (state->running == 0 || state->period_us == 0u) {
        return;
    }
    if (now < state->next_expire_us) {
        return;
    }

    /* 无回调的周期定时：算术合并错过的周期，一次性推进 */
    if (state->callback == NULL && state->mode == BM_HRTIMER_MODE_PERIODIC) {
        uint64_t missed =
            (now - state->next_expire_us) / state->period_us + 1u;

        state->stats.irq_count += (uint32_t)missed;
        if (now > state->next_expire_us + BM_NATIVE_HRTIMER_MIN_PERIOD_US) {
            state->stats.deadline_miss_count += (uint32_t)missed;
        }
        state->next_expire_us += missed * state->period_us;
        return;
    }

    while (state->running != 0 && now >= state->next_expire_us) {
        uint64_t expire_before = state->next_expire_us;
        int      was_oneshot   = (state->mode == BM_HRTIMER_MODE_ONESHOT) ? 1 : 0;

        state->stats.irq_count++;
        if (now > state->next_expire_us + BM_NATIVE_HRTIMER_MIN_PERIOD_US) {
            state->stats.deadline_miss_count++;
        }

        if (state->callback != NULL) {
            /* 与真实 Hardware HRT 端口一致（bm_critical_wrap.h 契约）：
             * 回调派发标记 HRT ISR 上下文，避免"仿真放行、真机拒绝"分叉 */
            bm_hrt_isr_enter();
            state->callback(state->dev, state->user);
            bm_hrt_isr_exit();
        }

        if (state->mode == BM_HRTIMER_MODE_PERIODIC) {
            /* 回调内 stop：尊重，不再自增也不再触发 */
            if (state->running == 0) {
                break;
            }
            /* 回调内重新 start/set_compare（next_expire 已被改动）：不自增 */
            if (state->next_expire_us == expire_before) {
                state->next_expire_us += state->period_us;
            }
        } else if (was_oneshot != 0) {
            /* ONESHOT：回调内若已 start/set_compare 重武装则保留 running */
            if (state->running == 0
                || state->next_expire_us == expire_before) {
                state->running = 0;
            }
            break;
        }

        fired++;
        /* 单次 advance 回调派发达上限：剩余周期计入 deadline miss 并算术推进 */
        if (fired >= BM_NATIVE_HRTIMER_MAX_FIRE_PER_ADVANCE
            && state->running != 0
            && state->mode == BM_HRTIMER_MODE_PERIODIC
            && now >= state->next_expire_us) {
            uint64_t remaining =
                (now - state->next_expire_us) / state->period_us + 1u;

            state->stats.deadline_miss_count += (uint32_t)remaining;
            state->next_expire_us += remaining * state->period_us;
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

    s_virtual_now_us += delta_us;
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

/**
 * @brief 启动高分辨率定时器设备。
 * @param dev 高分辨率定时器 设备实例。
 * @param mode 定时器运行模式。
 * @param period_us 定时器周期，单位为微秒。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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

/**
 * @brief 停止高分辨率定时器设备。
 * @param dev 高分辨率定时器 设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int native_hrtimer_stop(const struct bm_hal_hrtimer *dev) {
    bm_native_hrtimer_state_t *state;

    state = bm_native_hrtimer_state_for(dev);
    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    state->running = 0;
    return BM_OK;
}

/**
 * @brief 设置高分辨率定时器比较时刻。
 * @param dev 高分辨率定时器 设备实例。
 * @param compare_us 比较时刻，单位为微秒。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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
    /* 仅更新下一次到期时刻：保留当前 mode 与 running 状态，不隐含启动 */
    state->period_us = compare_us;
    state->next_expire_us = now + compare_us;
    return BM_OK;
}

/**
 * @brief 读取当前定时器频率。
 * @param dev 高分辨率定时器 设备实例；当前实现不使用该参数。
 * @return 定时器频率，单位为 Hz；设备无效时返回 0。
 */
static uint32_t native_hrtimer_get_freq(const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return BM_NATIVE_HRTIMER_FREQ_HZ;
}

/**
 * @brief 读取高分辨率定时器分辨率。
 * @param dev 高分辨率定时器 设备实例；当前实现不使用该参数。
 * @return 定时器分辨率，单位为纳秒；设备无效时返回 0。
 */
static uint32_t native_hrtimer_get_resolution_ns(
    const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return 1000u; /* 1MHz => 1000ns/tick */
}

/**
 * @brief 读取高分辨率定时器支持的最大周期。
 * @param dev 高分辨率定时器 设备实例；当前实现不使用该参数。
 * @return 支持的最大周期，单位为微秒；设备无效时返回 0。
 */
static uint32_t native_hrtimer_get_max_period_us(
    const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return BM_NATIVE_HRTIMER_MAX_PERIOD_US;
}

/**
 * @brief 读取高分辨率定时器支持的最小周期。
 * @param dev 高分辨率定时器 设备实例；当前实现不使用该参数。
 * @return 支持的最小周期，单位为微秒；设备无效时返回 0。
 */
static uint32_t native_hrtimer_get_min_period_us(
    const struct bm_hal_hrtimer *dev) {
    (void)dev;
    return BM_NATIVE_HRTIMER_MIN_PERIOD_US;
}

/**
 * @brief 读取高分辨率定时器运行统计。
 * @param dev 高分辨率定时器 设备实例。
 * @param stats 用于接收运行统计的输出结构；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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

/**
 * @brief 设置高分辨率定时器回调。
 * @param dev 高分辨率定时器 设备实例。
 * @param cb tick 回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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

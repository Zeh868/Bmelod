/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_hrtimer_stm32g4.c
 * @brief STM32G4 LL 高精度 Timer 后端
 * @maturity E1
 *
 * 支持多实例、周期/单次/Output Compare、动态改比较值、计数器回绕处理。
 * App 通过 `bm_hrtimer_stm32g4_config_t` 指定 TIM/通道/IRQ，Bmelod 不固定 TIM 编号。
 *
 * ISR 有界：仅清除标志、更新 compare、递增统计、派发回调；不解析业务协议。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 高精度 Timer 后端
 * 2026-07-28       1.1            zeh            deadline_miss 改超期阈值判定（原条件恒真）；
 *                                                删除 TIM6 handler（TIM6_DAC_IRQn 被 tick 占用）；
 *                                                init 支持 config 覆盖入参、补 initialized
 *                                                标志与失败回滚；ctx_for 改按 dev 匹配
 * 2026-07-29       1.2            zeh            删除未使用的 bm_vendor_hrtimer_get_ccr()
 * 2026-07-31       1.3            zeh            ISR 回调派发首尾成对调用
 *                                                bm_hrt_isr_enter/exit，落地 Hardware HRT
 *                                                端口的掩码模式拦截契约
 * 2026-08-01       1.3            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_hrtimer_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"
#include "bm_critical_wrap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_tim.h"

/** @brief 最大 Timer 实例数。 */
#define BM_VENDOR_HRTIMER_INSTANCE_COUNT 2u

/** @brief 计数器满量程（32 位向上计数）。 */
#define BM_HRTIMER_CNT_MAX 0xFFFFFFFFu

/** @brief 单个 Timer 运行时上下文。 */
typedef struct {
    const struct bm_hal_hrtimer       *dev;
    const bm_hrtimer_stm32g4_config_t *cfg;
    uint32_t                           mode;
    uint32_t                           period_ticks;
    uint32_t                           next_compare;
    bm_hrtimer_callback_t              callback;
    void                              *user;
    int                                initialized;
    int                                running;
    bm_hrtimer_stats_t                 stats;
} bm_vendor_hrtimer_context_t;

static bm_vendor_hrtimer_context_t g_hrtimer_ctx[BM_VENDOR_HRTIMER_INSTANCE_COUNT];

/** @brief 默认 TIM2 配置（CH1，APB1）。 */
static const bm_hrtimer_stm32g4_config_t g_hrtimer_cfg_0 = {
    .tim = TIM2,
    .channel = LL_TIM_CHANNEL_CH1,
    .irqn = TIM2_IRQn,
    .rcc_apb1 = LL_APB1_GRP1_PERIPH_TIM2,
    .rcc_apb2 = 0u,
    .prescaler = 0u,
    .auto_reload = BM_HRTIMER_CNT_MAX,
    .irq_priority = 2u,
};

/** @brief 默认 TIM3 配置（CH1，APB1）。 */
static const bm_hrtimer_stm32g4_config_t g_hrtimer_cfg_1 = {
    .tim = TIM3,
    .channel = LL_TIM_CHANNEL_CH1,
    .irqn = TIM3_IRQn,
    .rcc_apb1 = LL_APB1_GRP1_PERIPH_TIM3,
    .rcc_apb2 = 0u,
    .prescaler = 0u,
    .auto_reload = 0xFFFFu, /* TIM3 是 16 位 */
    .irq_priority = 2u,
};

/**
 * @brief 由设备实例获取运行时上下文。
 */
static bm_vendor_hrtimer_context_t *bm_vendor_hrtimer_ctx_for(
    const struct bm_hal_hrtimer *dev) {
    bm_vendor_hrtimer_context_t *free_slot = NULL;
    uint32_t i;

    if (dev == NULL) {
        return NULL;
    }
    /* 按 dev 指针匹配：init 允许用 config 入参覆盖 dev->config，
     * 按 cfg 匹配会在覆盖场景失配 */
    for (i = 0u; i < BM_VENDOR_HRTIMER_INSTANCE_COUNT; ++i) {
        if (g_hrtimer_ctx[i].dev == dev) {
            return &g_hrtimer_ctx[i];
        }
        if (free_slot == NULL && g_hrtimer_ctx[i].cfg == NULL) {
            free_slot = &g_hrtimer_ctx[i];
        }
    }
    return free_slot;
}

/**
 * @brief 获取 Timer 输入时钟（Hz）。
 *
 * APB1/2 定时器时钟：当对应 APB 分频 >1 时倍频（RM0440 规则）。
 */
static uint32_t bm_vendor_hrtimer_tim_clk_hz(const bm_hrtimer_stm32g4_config_t *cfg) {
    LL_RCC_ClocksTypeDef clocks;
    uint32_t pclk;

    LL_RCC_GetSystemClocksFreq(&clocks);
    if (cfg->rcc_apb2 != 0u) {
        pclk = clocks.PCLK2_Frequency;
        return (LL_RCC_GetAPB2Prescaler() == LL_RCC_APB2_DIV_1) ? pclk : (pclk * 2u);
    }
    pclk = clocks.PCLK1_Frequency;
    return (LL_RCC_GetAPB1Prescaler() == LL_RCC_APB1_DIV_1) ? pclk : (pclk * 2u);
}

/**
 * @brief 计算当前有效计数频率（Hz）。
 */
static uint32_t bm_vendor_hrtimer_freq_hz(const bm_hrtimer_stm32g4_config_t *cfg) {
    uint32_t clk = bm_vendor_hrtimer_tim_clk_hz(cfg);
    uint32_t psc = cfg->prescaler + 1u;

    return clk / psc;
}

/**
 * @brief 将微秒转换为 tick 数（向上取整）。
 */
static uint32_t bm_vendor_hrtimer_us_to_ticks(const bm_hrtimer_stm32g4_config_t *cfg,
                                              uint32_t us) {
    uint64_t freq = bm_vendor_hrtimer_freq_hz(cfg);
    uint64_t ticks = ((uint64_t)us * freq + 999999u) / 1000000u;

    if (ticks > cfg->auto_reload) {
        ticks = cfg->auto_reload;
    }
    if (ticks == 0u) {
        ticks = 1u;
    }
    return (uint32_t)ticks;
}

/**
 * @brief 将 tick 数转换为微秒（向下取整）。
 */
static uint32_t bm_vendor_hrtimer_ticks_to_us(const bm_hrtimer_stm32g4_config_t *cfg,
                                              uint32_t ticks) {
    uint64_t freq = bm_vendor_hrtimer_freq_hz(cfg);

    return (uint32_t)((uint64_t)ticks * 1000000u / freq);
}

/**
 * @brief 写指定通道的比较寄存器。
 */
static void bm_vendor_hrtimer_set_ccr(TIM_TypeDef *tim, uint32_t channel,
                                      uint32_t value) {
    switch (channel) {
    case LL_TIM_CHANNEL_CH1:
        LL_TIM_OC_SetCompareCH1(tim, value);
        break;
    case LL_TIM_CHANNEL_CH2:
        LL_TIM_OC_SetCompareCH2(tim, value);
        break;
    case LL_TIM_CHANNEL_CH3:
        LL_TIM_OC_SetCompareCH3(tim, value);
        break;
    case LL_TIM_CHANNEL_CH4:
        LL_TIM_OC_SetCompareCH4(tim, value);
        break;
    default:
        break;
    }
}

/**
 * @brief 使能指定通道的比较中断。
 */
static void bm_vendor_hrtimer_enable_it_cc(TIM_TypeDef *tim, uint32_t channel) {
    switch (channel) {
    case LL_TIM_CHANNEL_CH1:
        LL_TIM_EnableIT_CC1(tim);
        break;
    case LL_TIM_CHANNEL_CH2:
        LL_TIM_EnableIT_CC2(tim);
        break;
    case LL_TIM_CHANNEL_CH3:
        LL_TIM_EnableIT_CC3(tim);
        break;
    case LL_TIM_CHANNEL_CH4:
        LL_TIM_EnableIT_CC4(tim);
        break;
    default:
        break;
    }
}

/**
 * @brief 禁止指定通道的比较中断。
 */
static void bm_vendor_hrtimer_disable_it_cc(TIM_TypeDef *tim, uint32_t channel) {
    switch (channel) {
    case LL_TIM_CHANNEL_CH1:
        LL_TIM_DisableIT_CC1(tim);
        break;
    case LL_TIM_CHANNEL_CH2:
        LL_TIM_DisableIT_CC2(tim);
        break;
    case LL_TIM_CHANNEL_CH3:
        LL_TIM_DisableIT_CC3(tim);
        break;
    case LL_TIM_CHANNEL_CH4:
        LL_TIM_DisableIT_CC4(tim);
        break;
    default:
        break;
    }
}

/**
 * @brief 清除指定通道的比较中断标志。
 */
static void bm_vendor_hrtimer_clear_flag_cc(TIM_TypeDef *tim, uint32_t channel) {
    switch (channel) {
    case LL_TIM_CHANNEL_CH1:
        LL_TIM_ClearFlag_CC1(tim);
        break;
    case LL_TIM_CHANNEL_CH2:
        LL_TIM_ClearFlag_CC2(tim);
        break;
    case LL_TIM_CHANNEL_CH3:
        LL_TIM_ClearFlag_CC3(tim);
        break;
    case LL_TIM_CHANNEL_CH4:
        LL_TIM_ClearFlag_CC4(tim);
        break;
    default:
        break;
    }
}

/**
 * @brief 检测指定通道的比较中断是否 active。
 */
static uint32_t bm_vendor_hrtimer_is_active_flag_cc(TIM_TypeDef *tim,
                                                    uint32_t channel) {
    switch (channel) {
    case LL_TIM_CHANNEL_CH1:
        return LL_TIM_IsActiveFlag_CC1(tim);
    case LL_TIM_CHANNEL_CH2:
        return LL_TIM_IsActiveFlag_CC2(tim);
    case LL_TIM_CHANNEL_CH3:
        return LL_TIM_IsActiveFlag_CC3(tim);
    case LL_TIM_CHANNEL_CH4:
        return LL_TIM_IsActiveFlag_CC4(tim);
    default:
        return 0u;
    }
}

/**
 * @brief 使能 Timer 时钟。
 */
static void bm_vendor_hrtimer_enable_clock(const bm_hrtimer_stm32g4_config_t *cfg) {
    if (cfg->rcc_apb1 != 0u) {
        LL_APB1_GRP1_EnableClock(cfg->rcc_apb1);
    }
    if (cfg->rcc_apb2 != 0u) {
        LL_APB2_GRP1_EnableClock(cfg->rcc_apb2);
    }
}

/**
 * @brief 初始化 Timer 硬件（幂等）。
 */
/**
 * @brief 判断 TIM 是否为 32 位计数器。
 */
static int bm_vendor_hrtimer_is_32bit(TIM_TypeDef *tim) {
    return (tim == TIM2 || tim == TIM5) ? 1 : 0;
}

/**
 * @brief 校验配置合法性。
 */
static int bm_vendor_hrtimer_validate_config(
    const bm_hrtimer_stm32g4_config_t *cfg) {
    uint32_t max_arr;

    if (cfg == NULL || cfg->tim == NULL || cfg->channel == 0u) {
        return BM_ERR_INVALID;
    }
    if (cfg->prescaler > 0xFFFFu) {
        return BM_ERR_INVALID;
    }
    if (cfg->channel != LL_TIM_CHANNEL_CH1 &&
        cfg->channel != LL_TIM_CHANNEL_CH2 &&
        cfg->channel != LL_TIM_CHANNEL_CH3 &&
        cfg->channel != LL_TIM_CHANNEL_CH4) {
        return BM_ERR_INVALID;
    }

    max_arr = bm_vendor_hrtimer_is_32bit(cfg->tim) ? 0xFFFFFFFFu : 0xFFFFu;
    if (cfg->auto_reload == 0u || cfg->auto_reload > max_arr) {
        return BM_ERR_INVALID;
    }
    if (cfg->irqn < 0) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 初始化 STM32G4 高分辨率定时器硬件上下文。
 * @param ctx 设备驱动上下文；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_hrtimer_hw_init(bm_vendor_hrtimer_context_t *ctx) {
    const bm_hrtimer_stm32g4_config_t *cfg = ctx->cfg;

    if (bm_vendor_hrtimer_validate_config(cfg) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_vendor_hrtimer_enable_clock(cfg);

    LL_TIM_SetPrescaler(cfg->tim, cfg->prescaler);
    LL_TIM_SetAutoReload(cfg->tim, cfg->auto_reload);
    LL_TIM_SetCounterMode(cfg->tim, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetClockDivision(cfg->tim, LL_TIM_CLOCKDIVISION_DIV1);
    LL_TIM_DisableARRPreload(cfg->tim);

    LL_TIM_OC_SetMode(cfg->tim, cfg->channel, LL_TIM_OCMODE_ACTIVE);
    LL_TIM_OC_DisablePreload(cfg->tim, cfg->channel);
    bm_vendor_hrtimer_set_ccr(cfg->tim, cfg->channel, cfg->auto_reload);

    LL_TIM_ClearFlag_UPDATE(cfg->tim);
    LL_TIM_DisableIT_UPDATE(cfg->tim);

    NVIC_SetPriority(cfg->irqn, cfg->irq_priority);
    return BM_OK;
}

/**
 * @brief 公共 ISR 处理。
 */
static void bm_vendor_hrtimer_isr(bm_vendor_hrtimer_context_t *ctx) {
    TIM_TypeDef *tim;
    uint32_t cnt;
    uint32_t next;
    uint32_t arr;

    if (ctx == NULL || ctx->cfg == NULL) {
        return;
    }
    tim = ctx->cfg->tim;
    arr = ctx->cfg->auto_reload;

    if (bm_vendor_hrtimer_is_active_flag_cc(tim, ctx->cfg->channel) == 0u) {
        return;
    }
    bm_vendor_hrtimer_clear_flag_cc(tim, ctx->cfg->channel);

    ctx->stats.irq_count++;

    cnt = LL_TIM_GetCounter(tim);
    /* 检测计数器回绕 */
    if (cnt < ctx->next_compare && ctx->stats.irq_count > 1u) {
        ctx->stats.wrap_count++;
    }
    /* deadline miss：ISR 延迟超过一个完整周期才计 miss（正常触发时
     * cnt 与 next_compare 之差仅为中断延迟，远小于 period_ticks；
     * 原 <= arr/2 条件在每次正常 ISR 中恒真） */
    if (ctx->running != 0 && ctx->period_ticks != 0u &&
        ((uint32_t)(cnt - ctx->next_compare) > ctx->period_ticks)) {
        ctx->stats.deadline_miss_count++;
    }

    if (ctx->mode == BM_HRTIMER_MODE_PERIODIC && ctx->running != 0) {
        /* 下一个比较点 = 当前计数器 + 周期；处理回绕 */
        next = cnt + ctx->period_ticks;
        if (next > arr) {
            next = next - arr - 1u;
        }
        ctx->next_compare = next;
        bm_vendor_hrtimer_set_ccr(tim, ctx->cfg->channel, next);
    } else {
        ctx->running = 0;
        bm_vendor_hrtimer_disable_it_cc(tim, ctx->cfg->channel);
    }

    if (ctx->callback != NULL) {
        /* Hardware HRT 端口契约（bm_critical_wrap.h）：回调派发首尾成对
         * 标记 HRT ISR 上下文，使掩码模式的 fail-closed 拦截生效 */
        bm_hrt_isr_enter();
        ctx->callback(ctx->dev, ctx->user);
        bm_hrt_isr_exit();
    }
}

/* ---------- IRQ handlers ---------- */

#define BM_HRTIMER_DEFINE_HANDLER(tim_inst)                          \
    void tim_inst##_IRQHandler(void) {                               \
        uint32_t i;                                                  \
        for (i = 0u; i < BM_VENDOR_HRTIMER_INSTANCE_COUNT; ++i) {    \
            if (g_hrtimer_ctx[i].cfg != NULL &&                      \
                g_hrtimer_ctx[i].cfg->tim == tim_inst) {             \
                bm_vendor_hrtimer_isr(&g_hrtimer_ctx[i]);            \
            }                                                        \
        }                                                            \
    }

BM_HRTIMER_DEFINE_HANDLER(TIM2)
BM_HRTIMER_DEFINE_HANDLER(TIM3)
BM_HRTIMER_DEFINE_HANDLER(TIM4)
BM_HRTIMER_DEFINE_HANDLER(TIM5)
/* TIM6 保留：G4 上 TIM6 中断向量是 TIM6_DAC_IRQn，已被系统 tick singleton
 * 占用（bm_hal_instances_stm32g4.h 默认 tick 定时器），本后端不提供 TIM6
 * handler；需要 hrtimer 的 TIM6 场景须先将 tick 切到 TIM7。 */
BM_HRTIMER_DEFINE_HANDLER(TIM7)

/* ---------- HAL API 实现 ---------- */

static int bm_vendor_hrtimer_init(const struct bm_hal_hrtimer *dev, void *config) {
    bm_vendor_hrtimer_context_t *ctx;
    const bm_hrtimer_stm32g4_config_t *cfg;
    int rc;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    /* App 可通过 config 实参覆盖 dev->config；NULL 则使用设备默认值 */
    cfg = (config != NULL)
              ? (const bm_hrtimer_stm32g4_config_t *)config
              : (const bm_hrtimer_stm32g4_config_t *)dev->config;
    if (cfg == NULL) {
        return BM_ERR_INVALID;
    }
    ctx->dev = dev;
    ctx->cfg = cfg;
    ctx->initialized = 0;
    ctx->running = 0;
    ctx->mode = BM_HRTIMER_MODE_PERIODIC;
    ctx->period_ticks = 0u;
    ctx->next_compare = 0u;
    ctx->callback = NULL;
    ctx->user = NULL;
    (void)memset(&ctx->stats, 0, sizeof(ctx->stats));

    rc = bm_vendor_hrtimer_hw_init(ctx);
    if (rc != BM_OK) {
        /* 失败回滚：清 cfg，避免残留半初始化上下文被后续调用命中 */
        ctx->cfg = NULL;
        return rc;
    }
    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 启动高分辨率定时器设备。
 * @param dev 高分辨率定时器 设备实例。
 * @param mode 定时器运行模式。
 * @param period_us 定时器周期，单位为微秒。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_hrtimer_start(const struct bm_hal_hrtimer *dev,
                                   uint32_t mode, uint32_t period_us) {
    bm_vendor_hrtimer_context_t *ctx;
    const bm_hrtimer_stm32g4_config_t *cfg;
    uint32_t ticks;
    uint32_t cnt;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (mode != BM_HRTIMER_MODE_PERIODIC && mode != BM_HRTIMER_MODE_ONESHOT) {
        return BM_ERR_INVALID;
    }

    cfg = ctx->cfg;
    ticks = bm_vendor_hrtimer_us_to_ticks(cfg, period_us);
    if (ticks == 0u || ticks > cfg->auto_reload) {
        return BM_ERR_INVALID;
    }

    ctx->mode = mode;
    ctx->period_ticks = ticks;

    LL_TIM_SetCounter(cfg->tim, 0u);
    cnt = LL_TIM_GetCounter(cfg->tim);
    ctx->next_compare = cnt + ticks;
    bm_vendor_hrtimer_set_ccr(cfg->tim, cfg->channel, ctx->next_compare);

    bm_vendor_hrtimer_clear_flag_cc(cfg->tim, cfg->channel);
    LL_TIM_EnableCounter(cfg->tim);
    NVIC_EnableIRQ(cfg->irqn);
    bm_vendor_hrtimer_enable_it_cc(cfg->tim, cfg->channel);

    ctx->running = 1;
    return BM_OK;
}

/**
 * @brief 停止高分辨率定时器设备。
 * @param dev 高分辨率定时器 设备实例。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_hrtimer_stop(const struct bm_hal_hrtimer *dev) {
    bm_vendor_hrtimer_context_t *ctx;
    TIM_TypeDef *tim;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    tim = ctx->cfg->tim;
    ctx->running = 0;
    bm_vendor_hrtimer_disable_it_cc(tim, ctx->cfg->channel);
    bm_vendor_hrtimer_clear_flag_cc(tim, ctx->cfg->channel);
    LL_TIM_ClearFlag_UPDATE(tim);
    return BM_OK;
}

/**
 * @brief 设置高分辨率定时器比较时刻。
 * @param dev 高分辨率定时器 设备实例。
 * @param compare_us 比较时刻，单位为微秒。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_hrtimer_set_compare(const struct bm_hal_hrtimer *dev,
                                         uint32_t compare_us) {
    bm_vendor_hrtimer_context_t *ctx;
    const bm_hrtimer_stm32g4_config_t *cfg;
    uint32_t ticks;
    uint32_t cnt;
    uint32_t next;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = ctx->cfg;
    ticks = bm_vendor_hrtimer_us_to_ticks(cfg, compare_us);
    if (ticks == 0u || ticks > cfg->auto_reload) {
        return BM_ERR_INVALID;
    }

    cnt = LL_TIM_GetCounter(cfg->tim);
    next = cnt + ticks;
    if (next > cfg->auto_reload) {
        next = next - cfg->auto_reload - 1u;
    }
    /* 若目标已过期（计数器已跑过），则推到最近的未来，避免等一整圈 */
    if (next <= cnt && (cnt - next) <= (cfg->auto_reload / 2u)) {
        next = cnt + 2u;
        if (next > cfg->auto_reload) {
            next = next - cfg->auto_reload - 1u;
        }
    }
    ctx->next_compare = next;
    bm_vendor_hrtimer_set_ccr(cfg->tim, cfg->channel, ctx->next_compare);

    if (ctx->running == 0) {
        LL_TIM_EnableCounter(cfg->tim);
        NVIC_EnableIRQ(cfg->irqn);
        bm_vendor_hrtimer_enable_it_cc(cfg->tim, cfg->channel);
        ctx->running = 1;
    }
    return BM_OK;
}

/**
 * @brief 读取当前定时器频率。
 * @param dev 高分辨率定时器 设备实例。
 * @return 定时器频率，单位为 Hz；设备无效时返回 0。
 */
static uint32_t bm_vendor_hrtimer_get_freq(const struct bm_hal_hrtimer *dev) {
    bm_vendor_hrtimer_context_t *ctx;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return 0u;
    }
    return bm_vendor_hrtimer_freq_hz(ctx->cfg);
}

/**
 * @brief 读取高分辨率定时器分辨率。
 * @param dev 高分辨率定时器 设备实例。
 * @return 定时器分辨率，单位为纳秒；设备无效时返回 0。
 */
static uint32_t bm_vendor_hrtimer_get_resolution_ns(
    const struct bm_hal_hrtimer *dev) {
    bm_vendor_hrtimer_context_t *ctx;
    uint32_t freq;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return 0u;
    }
    freq = bm_vendor_hrtimer_freq_hz(ctx->cfg);
    if (freq == 0u) {
        return 0u;
    }
    return 1000000000u / freq;
}

/**
 * @brief 读取高分辨率定时器支持的最大周期。
 * @param dev 高分辨率定时器 设备实例。
 * @return 支持的最大周期，单位为微秒；设备无效时返回 0。
 */
static uint32_t bm_vendor_hrtimer_get_max_period_us(
    const struct bm_hal_hrtimer *dev) {
    bm_vendor_hrtimer_context_t *ctx;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return 0u;
    }
    return bm_vendor_hrtimer_ticks_to_us(ctx->cfg, ctx->cfg->auto_reload);
}

/**
 * @brief 读取高分辨率定时器支持的最小周期。
 * @param dev 高分辨率定时器 设备实例。
 * @return 支持的最小周期，单位为微秒；设备无效时返回 0。
 */
static uint32_t bm_vendor_hrtimer_get_min_period_us(
    const struct bm_hal_hrtimer *dev) {
    bm_vendor_hrtimer_context_t *ctx;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || ctx->cfg == NULL) {
        return 0u;
    }
    return bm_vendor_hrtimer_ticks_to_us(ctx->cfg, 1u);
}

/**
 * @brief 读取高分辨率定时器运行统计。
 * @param dev 高分辨率定时器 设备实例。
 * @param stats 用于接收运行统计的输出结构；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_hrtimer_get_stats(const struct bm_hal_hrtimer *dev,
                                       bm_hrtimer_stats_t *stats) {
    bm_vendor_hrtimer_context_t *ctx;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL || stats == NULL) {
        return BM_ERR_INVALID;
    }
    *stats = ctx->stats;
    return BM_OK;
}

/**
 * @brief 设置高分辨率定时器回调。
 * @param dev 高分辨率定时器 设备实例。
 * @param cb tick 回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_hrtimer_set_callback(const struct bm_hal_hrtimer *dev,
                                          bm_hrtimer_callback_t cb, void *user) {
    bm_vendor_hrtimer_context_t *ctx;

    ctx = bm_vendor_hrtimer_ctx_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    ctx->callback = cb;
    ctx->user = user;
    return BM_OK;
}

static const struct bm_hrtimer_driver_api g_hrtimer_stm32g4_api = {
    bm_vendor_hrtimer_init,
    bm_vendor_hrtimer_start,
    bm_vendor_hrtimer_stop,
    bm_vendor_hrtimer_set_compare,
    bm_vendor_hrtimer_get_freq,
    bm_vendor_hrtimer_get_resolution_ns,
    bm_vendor_hrtimer_get_max_period_us,
    bm_vendor_hrtimer_get_min_period_us,
    bm_vendor_hrtimer_get_stats,
    bm_vendor_hrtimer_set_callback,
};

const bm_hal_hrtimer_t bm_stm32g4_hrtimer0 = { &g_hrtimer_stm32g4_api,
                                               &g_hrtimer_cfg_0 };
const bm_hal_hrtimer_t bm_stm32g4_hrtimer1 = { &g_hrtimer_stm32g4_api,
                                               &g_hrtimer_cfg_1 };

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_pwm_stm32g4.c
 * @brief STM32G474xB 三相互补 PWM 驱动（TIM1 高级定时器，STM32 LL 库）
 *
 * 配置要点：
 *   - 中心对齐模式 1，ARPE + CCRx 预装，update 事件（计数谷底=低边采样窗口）
 *     统一装载占空比并经 TRGO2 触发 ADC 注入采样（联动 bm_vendor_adc_stm32g4.c）；
 *   - 互补输出 + 死区（LL_TIM_OC_SetDeadTime 写 BDTR.DTG，由
 *     BM_STM32G4_PWM_DEADTIME_NS 换算，仅支持 DTG 第一编码段 ≤127 个 tDTS，
 *     约 ≤747ns @170MHz，覆盖常规栅驱需求）；
 *   - 硬件过流保护：LL_TIM_EnableBreakInputSource(BKIN, BKCOMP1) 把 COMP1
 *     输出内部接入 TIM1_BKIN，break 触发即硬件关断 MOE
 *     （配合 bm_vendor_comp_stm32g4.c）；
 *   - update ISR 派发电流环回调（bind_update），派发前后加
 *     bm_arch_isr_fpu_enter/exit 守卫（armv7em 上 no-op）；
 *   - 契约铁律：bind_update 不得隐式使能输出（MOE/CCxE 仅 enable_outputs 置位）；
 *     binding==NULL 时先关中断源（DIER.UIE）再清回调。
 *
 * 实例绑定（TIM1/GPIO/频率/死区）全部走 bm_hal_instances_stm32g4.h 宏；
 * 默认定时器复用拓扑为高边 GPIOA（PA8/9/10）、低边 GPIOB（PB13/14/15），
 * 覆盖引脚宏时须保持同端口（端口切换属板级改动，实机核对 AF 编码）。
 *
 * 保留 CMSIS 写法的位置：NVIC 优先级/使能（LL 无 NVIC 抽象，用 CMSIS core
 * 的 NVIC_* 函数，逐处注释）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 * 2026-07-27       1.1            zeh            寄存器级改写为 STM32 LL 库实现（决策变更：提高可读性）
 *
 */
#include "bm_vendor_pwm_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"
#include "armv7em/bm_arch_isr_fpu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_tim.h"

/** @brief 电机 PWM 实例数（本期单电机 M0）。 */
#define BM_VENDOR_PWM_INSTANCE_COUNT  1u
/** @brief 每电机相数。 */
#define BM_VENDOR_PWM_PHASE_COUNT     3u

/** @brief 三相高低边通道组合（LL_TIM_CC_EnableChannel/DisableChannel 入参）。 */
#define BM_VENDOR_PWM_ALL_CHANNELS  (LL_TIM_CHANNEL_CH1 | LL_TIM_CHANNEL_CH1N \
                                     | LL_TIM_CHANNEL_CH2 | LL_TIM_CHANNEL_CH2N \
                                     | LL_TIM_CHANNEL_CH3 | LL_TIM_CHANNEL_CH3N)

typedef struct {
    /** @brief 实例编号（0=M0）。 */
    uint32_t id;
} bm_vendor_pwm_config_t;

typedef struct {
    /** @brief 当前占空比缓存（0..BM_STM32G4_PWM_DUTY_MAX）。 */
    uint16_t duty[BM_VENDOR_PWM_PHASE_COUNT];
    /** @brief 输出是否已使能。 */
    int outputs_enabled;
    /** @brief 硬件是否已初始化。 */
    int initialized;
    /** @brief HRT 更新回调绑定（update ISR 派发）。 */
    bm_hal_hrt_binding_t update_binding;
    /** @brief ISR FPU 现场保存区占位（armv7em 上守卫 no-op）。 */
    uint8_t fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));
} bm_vendor_pwm_context_t;

/** @brief M0 PWM 上下文。 */
static bm_vendor_pwm_context_t g_pwm_context[BM_VENDOR_PWM_INSTANCE_COUNT];
/** @brief M0 静态配置。 */
static const bm_vendor_pwm_config_t g_pwm_config_m0 = { 0u };

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_pwm_context_t *bm_vendor_pwm_context_for(const struct bm_hal_pwm *dev)
{
    const bm_vendor_pwm_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_pwm_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_PWM_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_pwm_context[cfg->id];
}

/**
 * @brief APB2 定时器时钟（TIM1 所在总线；分频 >1 时倍频，RM0440 规则）。
 * @return APB2 定时器时钟（Hz）。
 */
static uint32_t bm_vendor_pwm_tim_clk_hz(void)
{
    LL_RCC_ClocksTypeDef clocks;
    uint32_t pclk2;

    LL_RCC_GetSystemClocksFreq(&clocks);
    pclk2 = clocks.PCLK2_Frequency;
    return (LL_RCC_GetAPB2Prescaler() == LL_RCC_APB2_DIV_1) ? pclk2
                                                            : (pclk2 * 2u);
}

/**
 * @brief GPIO 复用配置（推挽、高速、无上下拉），AF 编码走 instances 宏。
 */
static void bm_vendor_pwm_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    uint32_t pin_mask = 1u << pin;

    LL_GPIO_SetPinMode(port, pin_mask, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinSpeed(port, pin_mask, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinPull(port, pin_mask, LL_GPIO_PULL_NO);
    if (pin < 8u) {
        LL_GPIO_SetAFPin_0_7(port, pin_mask, af);
    } else {
        LL_GPIO_SetAFPin_8_15(port, pin_mask, af);
    }
}

/**
 * @brief 初始化单个电机的 TIM1 硬件（幂等）。
 *
 * 只配置定时器/引脚/中断源，不置 MOE、不置 CCxE——输出使能是
 * enable_outputs 的专属职责（契约：绑定不得隐式使能输出）。
 *
 * @param ctx 板级上下文。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_pwm_hw_init(bm_vendor_pwm_context_t *ctx)
{
    uint32_t tim_clk;
    uint32_t arr;
    uint32_t dtg;

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }

    tim_clk = bm_vendor_pwm_tim_clk_hz();
    arr     = tim_clk / (2u * BM_STM32G4_PWM_FREQ_HZ);
    if (arr < 2u) {
        arr = 2u;
    }

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA
                             | LL_AHB2_GRP1_PERIPH_GPIOB);

    /* 三相高边（GPIOA：PA8/PA9/PA10）+ 低边（GPIOB：PB13/PB14/PB15），AF 走宏 */
    bm_vendor_pwm_gpio_af(GPIOA, BM_STM32G4_PWM_UH_PIN, BM_STM32G4_PWM_GPIO_AF);
    bm_vendor_pwm_gpio_af(GPIOA, BM_STM32G4_PWM_VH_PIN, BM_STM32G4_PWM_GPIO_AF);
    bm_vendor_pwm_gpio_af(GPIOA, BM_STM32G4_PWM_WH_PIN, BM_STM32G4_PWM_GPIO_AF);
    bm_vendor_pwm_gpio_af(GPIOB, BM_STM32G4_PWM_UL_PIN, BM_STM32G4_PWM_GPIO_AF);
    bm_vendor_pwm_gpio_af(GPIOB, BM_STM32G4_PWM_VL_PIN, BM_STM32G4_PWM_GPIO_AF);
    bm_vendor_pwm_gpio_af(GPIOB, BM_STM32G4_PWM_WL_PIN, BM_STM32G4_PWM_GPIO_AF);

    /* 时基：中心对齐模式 1（CMS=01），PSC=0，ARPE */
    LL_TIM_SetCounterMode(TIM1, LL_TIM_COUNTERMODE_CENTER_DOWN);
    LL_TIM_SetPrescaler(TIM1, 0u);
    LL_TIM_SetAutoReload(TIM1, arr - 1u);
    LL_TIM_SetRepetitionCounter(TIM1, 0u);
    LL_TIM_EnableARRPreload(TIM1);

    /* TRGO2 = update 事件 → ADC 注入触发（谷底=低边采样窗口） */
    LL_TIM_SetTriggerOutput2(TIM1, LL_TIM_TRGO2_UPDATE);

    /* OC1/2/3 PWM 模式 1 + 预装，初值 0 */
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH2, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetMode(TIM1, LL_TIM_CHANNEL_CH3, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH1);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH2);
    LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH3);
    LL_TIM_OC_SetCompareCH1(TIM1, 0u);
    LL_TIM_OC_SetCompareCH2(TIM1, 0u);
    LL_TIM_OC_SetCompareCH3(TIM1, 0u);
    LL_TIM_CC_DisableChannel(TIM1, BM_VENDOR_PWM_ALL_CHANNELS); /* CCxE 由 enable_outputs 置位 */

    /* COMP1 输出内部接入 TIM1_BKIN（硬件过流 break 源） */
    LL_TIM_EnableBreakInputSource(TIM1, LL_TIM_BREAK_INPUT_BKIN,
                                  LL_TIM_BKIN_SOURCE_BKCOMP1);

    /*
     * 死区：DTG 第一编码段（0..127 个 tDTS，tDTS=1/tim_clk）。
     * 超出量程则钳位并静默降级——常规栅驱死区（百 ns 级）远在该段内。
     */
    dtg = (uint32_t)(((uint64_t)BM_STM32G4_PWM_DEADTIME_NS * tim_clk) / 1000000000ull);
    if (dtg > 127u) {
        dtg = 127u;
    }
    LL_TIM_OC_SetDeadTime(TIM1, dtg);

    /* break：高有效（COMP 不反相输出=过流为高），无滤波；运行/空闲关断态选择；
     * 不置 AOE（break 后由 enable_outputs 手动重武装） */
    LL_TIM_ConfigBRK(TIM1, LL_TIM_BREAK_POLARITY_HIGH, 0u, 0u);
    LL_TIM_SetOffStates(TIM1, LL_TIM_OSSI_ENABLE, LL_TIM_OSSR_ENABLE);
    LL_TIM_EnableBRK(TIM1);
    LL_TIM_DisableAutomaticOutput(TIM1);

    /* NVIC 无 LL API（LL 不抽象中断控制器），用 CMSIS core 函数 */
    NVIC_SetPriority(TIM1_UP_TIM16_IRQn, BM_STM32G4_PWM_IRQ_PRIORITY);

    LL_TIM_ClearFlag_UPDATE(TIM1);
    LL_TIM_GenerateEvent_UPDATE(TIM1); /* 装载预装寄存器 */
    LL_TIM_ClearFlag_UPDATE(TIM1);     /* UG 会置 UIF，清掉避免空 ISR */
    LL_TIM_EnableCounter(TIM1);

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 写入单相比较值（占空比 0..DUTY_MAX 按比例映射到 ARR）。
 */
static void bm_vendor_pwm_write_ccr(uint32_t phase, uint16_t duty)
{
    uint32_t arr = LL_TIM_GetAutoReload(TIM1) + 1u;
    uint32_t ccr = ((uint32_t)duty * arr) / BM_STM32G4_PWM_DUTY_MAX;

    switch (phase) {
    case 0u: LL_TIM_OC_SetCompareCH1(TIM1, ccr); break;
    case 1u: LL_TIM_OC_SetCompareCH2(TIM1, ccr); break;
    default: LL_TIM_OC_SetCompareCH3(TIM1, ccr); break;
    }
}

/**
 * @brief TIM1 update ISR（与 TIM16 共享向量，先判 UIF）。
 *
 * 铁律：先清 UIF，再在 FPU 守卫内派发 update 回调（电流环）。
 * ADC 注入采样由 TRGO2 硬件触发，不在本 ISR 软件触发。
 */
void TIM1_UP_TIM16_IRQHandler(void)
{
    bm_vendor_pwm_context_t *ctx = &g_pwm_context[0];
    unsigned fpu_prev;

    if (LL_TIM_IsActiveFlag_UPDATE(TIM1) == 0u) {
        return;
    }
    LL_TIM_ClearFlag_UPDATE(TIM1);

    fpu_prev = bm_arch_isr_fpu_enter(ctx->fpu_sa);
    if (ctx->update_binding.callback != NULL) {
        ctx->update_binding.callback(ctx->update_binding.context);
    }
    bm_arch_isr_fpu_exit(ctx->fpu_sa, fpu_prev);
}

/* ---------- HAL API 实现 ---------- */

/**
 * @brief 设置指定相位占空比（0..BM_STM32G4_PWM_DUTY_MAX）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_pwm_set_duty(const struct bm_hal_pwm *dev,
                                  uint32_t phase, uint16_t duty)
{
    bm_vendor_pwm_context_t *ctx;

    if (phase >= BM_VENDOR_PWM_PHASE_COUNT || duty > BM_STM32G4_PWM_DUTY_MAX) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_INVALID;
    }
    ctx->duty[phase] = duty;
    bm_vendor_pwm_write_ccr(phase, duty);
    return BM_OK;
}

/**
 * @brief 使能/禁用三相输出（置/清 CCxE+CCxNE+MOE；禁用即归零占空比）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_pwm_enable_outputs(const struct bm_hal_pwm *dev, int enable)
{
    bm_vendor_pwm_context_t *ctx;
    uint32_t phase;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_INVALID;
    }
    ctx->outputs_enabled = enable ? 1 : 0;

    if (enable != 0) {
        /* 重武装前先清 break/update 挂起标志（COMP 触发过 break 时 MOE 被硬件锁死） */
        LL_TIM_ClearFlag_BRK(TIM1);
        LL_TIM_ClearFlag_UPDATE(TIM1);
        LL_TIM_CC_EnableChannel(TIM1, BM_VENDOR_PWM_ALL_CHANNELS);
        LL_TIM_EnableAllOutputs(TIM1);
    } else {
        for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
            ctx->duty[phase] = 0u;
            bm_vendor_pwm_write_ccr(phase, 0u);
        }
        LL_TIM_DisableAllOutputs(TIM1);
        LL_TIM_CC_DisableChannel(TIM1, BM_VENDOR_PWM_ALL_CHANNELS);
    }
    return BM_OK;
}

/**
 * @brief 请求硬件安全态：立即关 MOE + CC 使能，占空比归零。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_pwm_request_safe_state(const struct bm_hal_pwm *dev)
{
    bm_vendor_pwm_context_t *ctx;
    uint32_t phase;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_INVALID;
    }
    ctx->outputs_enabled = 0;
    for (phase = 0u; phase < BM_VENDOR_PWM_PHASE_COUNT; ++phase) {
        ctx->duty[phase] = 0u;
        bm_vendor_pwm_write_ccr(phase, 0u);
    }
    LL_TIM_DisableAllOutputs(TIM1);
    LL_TIM_CC_DisableChannel(TIM1, BM_VENDOR_PWM_ALL_CHANNELS);
    return BM_OK;
}

/**
 * @brief 绑定 PWM update 事件到 HRT 回调（电流环入口）。
 *
 * 契约：不得隐式使能输出（MOE/CCxE 本函数不触碰）；binding==NULL 时
 * 先关中断源（DIER.UIE + NVIC）再清回调。
 *
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_pwm_bind_update(const struct bm_hal_pwm *dev,
                                     const bm_hal_hrt_binding_t *binding)
{
    bm_vendor_pwm_context_t *ctx;

    ctx = bm_vendor_pwm_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_pwm_hw_init(ctx) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (binding == NULL) {
        LL_TIM_DisableIT_UPDATE(TIM1);
        NVIC_DisableIRQ(TIM1_UP_TIM16_IRQn); /* NVIC 无 LL API，用 CMSIS core 函数 */
        memset(&ctx->update_binding, 0, sizeof(ctx->update_binding));
        return BM_OK;
    }
    ctx->update_binding = *binding;
    LL_TIM_ClearFlag_UPDATE(TIM1);
    NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
    LL_TIM_EnableIT_UPDATE(TIM1);
    return BM_OK;
}

/** @brief PWM HAL 驱动 API 表。 */
static const struct bm_pwm_driver_api g_pwm_api = {
    bm_vendor_pwm_set_duty,
    bm_vendor_pwm_enable_outputs,
    bm_vendor_pwm_request_safe_state,
    bm_vendor_pwm_bind_update,
};

/** @brief M0 电机 PWM 实例。 */
const bm_hal_pwm_t bm_hal_pwm_m0 = { &g_pwm_api, &g_pwm_config_m0 };

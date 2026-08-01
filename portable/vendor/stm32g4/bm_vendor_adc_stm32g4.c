/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_adc_stm32g4.c
 * @brief STM32G474xB 相电流 ADC 驱动（ADC1 注入组，TIM1 TRGO2 硬件触发，STM32 LL 库）
 * @maturity E1
 *
 * 采样链路（与 bm_vendor_pwm_stm32g4.c 联动）：
 *   TIM1 update（中心对齐谷底=低边采样窗口）→ TRGO2 → ADC1 注入序列
 *   （ia/ib 双 rank）→ JEOS 中断 → read_injected 读取缓存值。
 * 全程硬件触发，ISR 不做软件启动转换，电流环时序由 TIM1 主控。
 *
 * 契约铁律：bind_complete 不得隐式启动 ADC 序列——注入触发武装
 * （JADSTART，LL_ADC_INJ_StartConversion 置位该位以放行外部触发）在
 * read_injected 首次懒初始化时完成，bind 仅开 JEOS 中断源；
 * binding==NULL 时先关 JEOS 中断源再清回调。
 *
 * 实例绑定（ADC 实例/通道/触发源编码）全部走 bm_hal_instances_stm32g4.h 宏。
 * ADC 时钟取 ADC12_COMMON 同步模式 HCLK/4（170MHz → 42.5MHz，低于 60MHz 上限）。
 *
 * 保留 CMSIS 写法的位置：NVIC 优先级/使能（LL 无 NVIC 抽象，用 CMSIS core
 * 的 NVIC_* 函数，逐处注释）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 * 2026-07-27       1.1            zeh            寄存器级改写为 STM32 LL 库实现（决策变更：提高可读性）；
 *                                                顺带修正触发源默认值：TIM1_TRGO2 的 JEXTSEL 编码为 8
 *                                                （LL_ADC_INJ_TRIG_EXT_TIM1_TRGO2），寄存器版误为 2
 * 2026-07-31       1.2            zeh            JEOS ISR 回调派发首尾成对调用
 *                                                bm_hrt_isr_enter/exit，落地 Hardware HRT
 *                                                端口的掩码模式拦截契约
 * 2026-08-01       1.2            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_adc_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"
#include "bm_critical_wrap.h"
#include "armv7em/bm_arch_isr_fpu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_adc.h"

/** @brief 电机 ADC 实例数（本期单电机 M0）。 */
#define BM_VENDOR_ADC_INSTANCE_COUNT  1u
/**
 * @brief 硬件就绪轮询上限（ADCAL / ADRDY 等待）。
 *
 * 各等待环节正常均在 µs 级完成；超限返回 BM_ERR_TIMEOUT 而非死循环
 * （对齐 core 实时约束：路径必须有界）。
 */
#define BM_VENDOR_ADC_POLL_LIMIT      100000u

/**
 * @brief 通道号（0..18）→ LL_ADC_CHANNEL_x 复合常量映射表。
 *
 * LL_ADC_CHANNEL_x 除通道号外还打包了 SMP 寄存器偏移/位偏移信息（供
 * LL_ADC_SetChannelSamplingTime / LL_ADC_INJ_SetSequencerRanks 内部解码），
 * 不能直接用裸通道号替代；instances 头宏是裸通道号，经本表转换。
 */
static const uint32_t s_adc_channel_ll[19] = {
    LL_ADC_CHANNEL_0,  LL_ADC_CHANNEL_1,  LL_ADC_CHANNEL_2,  LL_ADC_CHANNEL_3,
    LL_ADC_CHANNEL_4,  LL_ADC_CHANNEL_5,  LL_ADC_CHANNEL_6,  LL_ADC_CHANNEL_7,
    LL_ADC_CHANNEL_8,  LL_ADC_CHANNEL_9,  LL_ADC_CHANNEL_10, LL_ADC_CHANNEL_11,
    LL_ADC_CHANNEL_12, LL_ADC_CHANNEL_13, LL_ADC_CHANNEL_14, LL_ADC_CHANNEL_15,
    LL_ADC_CHANNEL_16, LL_ADC_CHANNEL_17, LL_ADC_CHANNEL_18,
};

typedef struct {
    /** @brief 实例编号（0=M0）。 */
    uint32_t id;
} bm_vendor_adc_config_t;

typedef struct {
    /** @brief 硬件是否已初始化。 */
    int initialized;
    /** @brief JEOS ISR 更新的注入采样缓存（12bit raw，板级零偏标定为已知缺口）。 */
    uint16_t cached[BM_STM32G4_ADC_RANK_COUNT];
    /** @brief HRT 完成回调绑定（JEOS ISR 派发）。 */
    bm_hal_hrt_binding_t complete_binding;
    /** @brief ISR FPU 现场保存区占位（armv7em 上守卫 no-op）。 */
    uint8_t fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));
} bm_vendor_adc_context_t;

/** @brief M0 ADC 上下文。 */
static bm_vendor_adc_context_t g_adc_context[BM_VENDOR_ADC_INSTANCE_COUNT];
/** @brief M0 静态配置。 */
static const bm_vendor_adc_config_t g_adc_config_m0 = { 0u };

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_adc_context_t *bm_vendor_adc_context_for(const struct bm_hal_adc *dev)
{
    const bm_vendor_adc_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_adc_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_ADC_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_adc_context[cfg->id];
}

/**
 * @brief GPIO 模拟输入配置（MODER=11，无上下拉）。
 */
static void bm_vendor_adc_gpio_analog(GPIO_TypeDef *port, uint32_t pin)
{
    uint32_t pin_mask = 1u << pin;

    LL_GPIO_SetPinMode(port, pin_mask, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(port, pin_mask, LL_GPIO_PULL_NO);
}

/**
 * @brief 初始化 ADC1 硬件（幂等，由 read_injected 懒调用）。
 *
 * 序列：时钟/GPIO → 退出 deep-power-down → 上电稳压器并等待稳定 →
 * 单端校准 → 采样时间 → 注入序列（JL=rank数-1，JSQ1=ia，JSQ2=ib）→
 * 外部触发（上升沿 + 默认 TIM1_TRGO2）→ ADEN 就绪 → JADSTART 武装触发。
 * 本函数不开任何中断源（JEOS 中断归 bind_complete）。
 *
 * @param ctx 板级上下文。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_TIMEOUT 硬件未就绪。
 */
static int bm_vendor_adc_hw_init(bm_vendor_adc_context_t *ctx)
{
    uint32_t wait;

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }
    if (BM_STM32G4_ADC_CH_IA > 18u || BM_STM32G4_ADC_CH_IB > 18u) {
        return BM_ERR_INVALID;
    }

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA
                             | LL_AHB2_GRP1_PERIPH_ADC12);

    bm_vendor_adc_gpio_analog(GPIOA, BM_STM32G4_ADC_IA_PIN);
    bm_vendor_adc_gpio_analog(GPIOA, BM_STM32G4_ADC_IB_PIN);

    /* ADC 时钟：同步模式 HCLK/4（42.5MHz @170MHz） */
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_SYNC_PCLK_DIV4);

    /* 退出 deep-power-down，上电内部稳压器 */
    LL_ADC_DisableDeepPowerDown(ADC1);
    LL_ADC_EnableInternalRegulator(ADC1);
    /* 稳压器启动约 10µs（LL_ADC_DELAY_INTERNAL_REGUL_STAB_US 量级；
     * 有界等待 ~2000 周期 @170MHz，LL 无对应的延迟原语、保留空转循环） */
    for (wait = 0u; wait < 2000u; ++wait) {
        __asm volatile ("nop");
    }

    /* 单端校准（要求 ADEN=0；上电初态即未使能） */
    LL_ADC_Disable(ADC1);
    LL_ADC_StartCalibration(ADC1, LL_ADC_SINGLE_ENDED);
    for (wait = 0u; wait < BM_VENDOR_ADC_POLL_LIMIT; ++wait) {
        if (LL_ADC_IsCalibrationOnGoing(ADC1) == 0u) {
            break;
        }
    }
    if (wait >= BM_VENDOR_ADC_POLL_LIMIT) {
        return BM_ERR_TIMEOUT;
    }

    /* 采样时间（LL_ADC_SAMPLINGTIME_* 常量数值即 SMP 裸码，直接传宏） */
    LL_ADC_SetChannelSamplingTime(ADC1, s_adc_channel_ll[BM_STM32G4_ADC_CH_IA],
                                  BM_STM32G4_ADC_SMP);
    LL_ADC_SetChannelSamplingTime(ADC1, s_adc_channel_ll[BM_STM32G4_ADC_CH_IB],
                                  BM_STM32G4_ADC_SMP);

    /* 注入序列：JL = rank数-1；JSQ1=ia，JSQ2=ib */
    LL_ADC_INJ_SetSequencerLength(ADC1,
        ((BM_STM32G4_ADC_RANK_COUNT - 1u) << ADC_JSQR_JL_Pos));
    LL_ADC_INJ_SetSequencerRanks(ADC1, LL_ADC_INJ_RANK_1,
                                 s_adc_channel_ll[BM_STM32G4_ADC_CH_IA]);
#if BM_STM32G4_ADC_RANK_COUNT > 1u
    LL_ADC_INJ_SetSequencerRanks(ADC1, LL_ADC_INJ_RANK_2,
                                 s_adc_channel_ll[BM_STM32G4_ADC_CH_IB]);
#endif

    /* 外部触发上升沿 + 触发源（默认 TIM1_TRGO2=JEXTSEL 8，RM0440 注入触发表） */
    LL_ADC_INJ_SetTriggerSource(ADC1,
        (BM_STM32G4_ADC_JEXTSEL << ADC_JSQR_JEXTSEL_Pos)
        | LL_ADC_INJ_TRIG_EXT_RISING);

    /* 使能并等待就绪 */
    LL_ADC_ClearFlag_ADRDY(ADC1);
    LL_ADC_Enable(ADC1);
    for (wait = 0u; wait < BM_VENDOR_ADC_POLL_LIMIT; ++wait) {
        if (LL_ADC_IsActiveFlag_ADRDY(ADC1) != 0u) {
            break;
        }
    }
    if (wait >= BM_VENDOR_ADC_POLL_LIMIT) {
        return BM_ERR_TIMEOUT;
    }

    /* 武装注入外部触发（置 JADSTART 放行触发检测，不启动转换：
     * 转换由 TIM1 TRGO2 硬件节拍触发） */
    LL_ADC_INJ_StartConversion(ADC1);

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief ADC1/2 全局中断：JEOS（注入序列完成）时更新缓存并派发完成回调。
 *
 * 铁律：读 JDR（顺带清 JEOC）、清 JEOS，均在 FPU 守卫外先完成；
 * 用户回调（电流环）在守卫内派发。
 */
void ADC1_2_IRQHandler(void)
{
    bm_vendor_adc_context_t *ctx = &g_adc_context[0];
    unsigned fpu_prev;

    if (LL_ADC_IsActiveFlag_JEOS(ADC1) == 0u) {
        return;
    }
    ctx->cached[0u] = LL_ADC_INJ_ReadConversionData12(ADC1, LL_ADC_INJ_RANK_1);
#if BM_STM32G4_ADC_RANK_COUNT > 1u
    ctx->cached[1u] = LL_ADC_INJ_ReadConversionData12(ADC1, LL_ADC_INJ_RANK_2);
#endif
    LL_ADC_ClearFlag_JEOS(ADC1);

    /* Hardware HRT 端口契约（bm_critical_wrap.h）：回调派发首尾成对标记
     * HRT ISR 上下文，使掩码模式对 SRT 队列 API 的 fail-closed 拦截在
     * 本链路生效；非掩码模式仅维护计数，不改变行为 */
    bm_hrt_isr_enter();
    fpu_prev = bm_arch_isr_fpu_enter(ctx->fpu_sa);
    if (ctx->complete_binding.callback != NULL) {
        ctx->complete_binding.callback(ctx->complete_binding.context);
    }
    bm_arch_isr_fpu_exit(ctx->fpu_sa, fpu_prev);
    bm_hrt_isr_exit();
}

/* ---------- HAL API 实现 ---------- */

/**
 * @brief 读取注入通道最近一次采样值（JEOS ISR 更新的缓存）。
 * @param dev   HAL 设备实例。
 * @param rank  采样序号（0=ia，1=ib）。
 * @param value 输出值（12bit raw；板级零偏中心化标定为已知缺口，见 README）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_TIMEOUT 硬件未就绪。
 */
static int bm_vendor_adc_read_injected(const struct bm_hal_adc *dev,
                                       uint32_t rank, uint16_t *value)
{
    bm_vendor_adc_context_t *ctx;
    int rc;

    if (value == NULL || rank >= BM_STM32G4_ADC_RANK_COUNT) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_adc_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_vendor_adc_hw_init(ctx);
    if (rc != BM_OK) {
        return rc;
    }
    *value = ctx->cached[rank];
    return BM_OK;
}

/**
 * @brief 绑定注入序列完成回调（JEOS 中断）。
 *
 * 契约：不启动 ADC 序列（JADSTART 在 read_injected 懒初始化中武装）；
 * binding==NULL 时先关 JEOS 中断源再清回调。
 *
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_TIMEOUT 硬件未就绪。
 */
static int bm_vendor_adc_bind_complete(const struct bm_hal_adc *dev,
                                       const bm_hal_hrt_binding_t *binding)
{
    bm_vendor_adc_context_t *ctx;
    int rc;

    ctx = bm_vendor_adc_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_vendor_adc_hw_init(ctx);
    if (rc != BM_OK) {
        return rc;
    }
    if (binding == NULL) {
        LL_ADC_DisableIT_JEOS(ADC1);
        NVIC_DisableIRQ(ADC1_2_IRQn); /* NVIC 无 LL API，用 CMSIS core 函数 */
        memset(&ctx->complete_binding, 0, sizeof(ctx->complete_binding));
        return BM_OK;
    }
    ctx->complete_binding = *binding;
    NVIC_SetPriority(ADC1_2_IRQn, BM_STM32G4_ADC_IRQ_PRIORITY);
    NVIC_EnableIRQ(ADC1_2_IRQn);
    LL_ADC_ClearFlag_JEOS(ADC1);
    LL_ADC_EnableIT_JEOS(ADC1);
    return BM_OK;
}

/** @brief ADC HAL 驱动 API 表。 */
static const struct bm_adc_driver_api g_adc_api = {
    bm_vendor_adc_read_injected,
    bm_vendor_adc_bind_complete,
};

/** @brief M0 电机 ADC 实例。 */
const bm_hal_adc_t bm_hal_adc_m0 = { &g_adc_api, &g_adc_config_m0 };

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_encoder_stm32g4.c
 * @brief STM32G474xB 增量编码器驱动（TIM3 正交编码器模式，STM32 LL 库）
 * @maturity E1
 *
 * TIM3 配置为编码器模式 3（LL_TIM_ENCODERMODE_X4_TI12，TI1/TI2 双沿计数），
 * 计数范围 4×CPR（一圈脉冲数，BM_STM32G4_ENC_CPR 可覆盖）。read 直接读 CNT，
 * 无中断、无滤波（输入滤波器 ICxF=0，板级按实际布线覆盖引脚宏）。
 *
 * 实例绑定（定时器/GPIO/CPR）全部走 bm_hal_instances_stm32g4.h 宏；
 * 默认定时器复用拓扑为 GPIOA（PA6/PA7，TIM3_CH1/CH2，AF2）。
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
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_encoder_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_tim.h"

/** @brief 编码器实例数（本期单电机 M0）。 */
#define BM_VENDOR_ENCODER_INSTANCE_COUNT  1u

typedef struct {
    /** @brief 实例编号（0=M0）。 */
    uint32_t id;
} bm_vendor_encoder_config_t;

typedef struct {
    /** @brief 硬件是否已初始化。 */
    int initialized;
} bm_vendor_encoder_context_t;

/** @brief M0 编码器上下文。 */
static bm_vendor_encoder_context_t g_encoder_context[BM_VENDOR_ENCODER_INSTANCE_COUNT];
/** @brief M0 静态配置。 */
static const bm_vendor_encoder_config_t g_encoder_config_m0 = { 0u };

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_encoder_context_t *bm_vendor_encoder_context_for(const struct bm_hal_encoder *dev)
{
    const bm_vendor_encoder_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_encoder_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_ENCODER_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_encoder_context[cfg->id];
}

/**
 * @brief GPIO 复用配置（推挽、高速、无上下拉）。
 */
static void bm_vendor_encoder_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
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
 * @brief 初始化 TIM3 编码器模式（幂等，由 read 懒调用）。
 *
 * @param ctx 板级上下文。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_encoder_hw_init(bm_vendor_encoder_context_t *ctx)
{
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

    bm_vendor_encoder_gpio_af(GPIOA, BM_STM32G4_ENC_A_PIN, BM_STM32G4_ENC_GPIO_AF);
    bm_vendor_encoder_gpio_af(GPIOA, BM_STM32G4_ENC_B_PIN, BM_STM32G4_ENC_GPIO_AF);

    /* TI1/TI2 直连输入、无滤波、不反相 */
    LL_TIM_IC_SetActiveInput(TIM3, LL_TIM_CHANNEL_CH1, LL_TIM_ACTIVEINPUT_DIRECTTI);
    LL_TIM_IC_SetActiveInput(TIM3, LL_TIM_CHANNEL_CH2, LL_TIM_ACTIVEINPUT_DIRECTTI);
    LL_TIM_IC_SetFilter(TIM3, LL_TIM_CHANNEL_CH1, LL_TIM_IC_FILTER_FDIV1);
    LL_TIM_IC_SetFilter(TIM3, LL_TIM_CHANNEL_CH2, LL_TIM_IC_FILTER_FDIV1);
    LL_TIM_IC_SetPolarity(TIM3, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_RISING);
    LL_TIM_IC_SetPolarity(TIM3, LL_TIM_CHANNEL_CH2, LL_TIM_IC_POLARITY_RISING);

    /* 编码器模式 3：TI1/TI2 双沿计数（×4） */
    LL_TIM_SetEncoderMode(TIM3, LL_TIM_ENCODERMODE_X4_TI12);
    LL_TIM_SetAutoReload(TIM3, BM_STM32G4_ENC_CPR * 4u - 1u);
    LL_TIM_SetCounter(TIM3, 0u);
    LL_TIM_EnableCounter(TIM3);

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 读取编码器计数（0..4×CPR-1，方向由 TIM3 硬件计数方向体现）。
 * @param dev   HAL 设备实例。
 * @param value 输出计数值。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_encoder_read(const struct bm_hal_encoder *dev, int32_t *value)
{
    bm_vendor_encoder_context_t *ctx;

    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    ctx = bm_vendor_encoder_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_encoder_hw_init(ctx) != BM_OK) {
        return BM_ERR_INVALID;
    }
    *value = (int32_t)LL_TIM_GetCounter(TIM3);
    return BM_OK;
}

/** @brief 编码器 HAL 驱动 API 表。 */
static const struct bm_encoder_driver_api g_encoder_api = {
    bm_vendor_encoder_read,
};

/** @brief M0 电机编码器实例。 */
const bm_hal_encoder_t bm_hal_encoder_m0 = { &g_encoder_api, &g_encoder_config_m0 };

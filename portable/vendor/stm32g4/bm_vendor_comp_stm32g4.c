/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_comp_stm32g4.c
 * @brief STM32G474xB 过流比较器驱动（COMP1 → TIM1_BKIN，STM32 LL 库）
 * @maturity E1
 *
 * 保护链路：相电流经采样电阻分压 → COMP1 同相端 → 超过反相端门限时
 * COMP1 输出翻转 → 经 TIM1_AF1.BKCMP1E（LL_TIM_EnableBreakInputSource，
 * 见 bm_vendor_pwm_stm32g4.c）内部直连 TIM1_BKIN → 硬件 break 立即关断
 * PWM MOE（无软件延迟）。门限/迟滞/blanking 编码全部走
 * bm_hal_instances_stm32g4.h 宏（板级按实际门限电路覆盖）。
 *
 * 注意：LL_COMP_INPUT_MINUS_* 的 VREFINT 系列常量自带 SCALEN/BRGEN 桥臂
 * 使能位（VREFINT 分压门限必需，纯写 INMSEL 域不会置位），本驱动经
 * LL_COMP_ConfigInputs 统一配置，规避手工漏位风险；门限若改用 DAC 通道
 * （编码 4/5），DAC 本体配置不在本驱动职责内（板级自备）。
 *
 * clear_latch 语义：COMP 触发 break 后 TIM1 MOE 被硬件锁死（本驱动不置
 * AOE），本函数清 TIM1 SR.BIF 解除 break 挂起锁存；输出不会自动恢复，
 * 须由上层确认故障消除后再调 bm_hal_pwm_enable_outputs 重武装。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 * 2026-07-27       1.1            zeh            寄存器级改写为 STM32 LL 库实现（决策变更：提高可读性）；
 *                                                顺带修正 VREFINT 门限缺 SCALEN/BRGEN 桥臂使能位的问题
 * 2026-08-01       1.1            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_comp_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_comp.h"
#include "stm32g4xx_ll_tim.h"

/** @brief 比较器实例数（本期单电机 M0）。 */
#define BM_VENDOR_COMP_INSTANCE_COUNT  1u

typedef struct {
    /** @brief 实例编号（0=M0）。 */
    uint32_t id;
} bm_vendor_comp_config_t;

typedef struct {
    /** @brief 硬件是否已初始化。 */
    int initialized;
} bm_vendor_comp_context_t;

/** @brief M0 比较器上下文。 */
static bm_vendor_comp_context_t g_comp_context[BM_VENDOR_COMP_INSTANCE_COUNT];
/** @brief M0 静态配置。 */
static const bm_vendor_comp_config_t g_comp_config_m0 = { 0u };

/**
 * @brief INMSEL 裸码（0..7）→ LL_COMP_INPUT_MINUS_* 常量映射表。
 *
 * VREFINT 系列（0..3）的 LL 常量自带 SCALEN/BRGEN 桥臂使能位；
 * 4=DAC3_CH1、5=DAC1_CH1（COMP1 门限用 DAC 通道时，DAC 本体由板级配置）；
 * 6/7=外部 IO1/IO2。
 */
static const uint32_t s_comp_inm_ll[8] = {
    LL_COMP_INPUT_MINUS_1_4VREFINT, /* 0 */
    LL_COMP_INPUT_MINUS_1_2VREFINT, /* 1（默认） */
    LL_COMP_INPUT_MINUS_3_4VREFINT, /* 2 */
    LL_COMP_INPUT_MINUS_VREFINT,    /* 3 */
    LL_COMP_INPUT_MINUS_DAC3_CH1,   /* 4 */
    LL_COMP_INPUT_MINUS_DAC1_CH1,   /* 5 */
    LL_COMP_INPUT_MINUS_IO1,        /* 6 */
    LL_COMP_INPUT_MINUS_IO2,        /* 7 */
};

/**
 * @brief 从设备实例提取板级上下文。
 * @param dev HAL 设备实例。
 * @return 板级上下文；无效时返回 NULL。
 */
static bm_vendor_comp_context_t *bm_vendor_comp_context_for(const struct bm_hal_comp *dev)
{
    const bm_vendor_comp_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_vendor_comp_config_t *)dev->config;
    if (cfg->id >= BM_VENDOR_COMP_INSTANCE_COUNT) {
        return NULL;
    }
    return &g_comp_context[cfg->id];
}

/**
 * @brief 初始化 COMP1（幂等，由 clear_latch 懒调用）。
 *
 * 按 instances 宏配置同相/反相输入、迟滞、极性、blanking 后使能。
 * COMP1 输出→TIM1_BKIN 的内部连接在 TIM1 侧（LL_TIM_EnableBreakInputSource，
 * bm_vendor_pwm_stm32g4.c）完成，本函数只负责比较器本体。
 *
 * @param ctx 板级上下文。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_comp_hw_init(bm_vendor_comp_context_t *ctx)
{
    uint32_t inp_ll;

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (ctx->initialized != 0) {
        return BM_OK;
    }
    if (BM_STM32G4_COMP_INMSEL > 7u || BM_STM32G4_COMP_INPSEL > 1u) {
        return BM_ERR_INVALID;
    }

    /* COMP1 属 APB2 域，随 SYSCFG 时钟门控（RM0440） */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    inp_ll = (BM_STM32G4_COMP_INPSEL == 0u) ? LL_COMP_INPUT_PLUS_IO1
                                            : LL_COMP_INPUT_PLUS_IO2;
    LL_COMP_ConfigInputs(COMP1, s_comp_inm_ll[BM_STM32G4_COMP_INMSEL], inp_ll);
    /* 迟滞/极性/blanking 的 LL 常量数值即对应 CSR 域的定位值，直接传宏 */
    LL_COMP_SetInputHysteresis(COMP1, BM_STM32G4_COMP_HYST << COMP_CSR_HYST_Pos);
    LL_COMP_SetOutputPolarity(COMP1, BM_STM32G4_COMP_POLARITY << COMP_CSR_POLARITY_Pos);
    LL_COMP_SetOutputBlankingSource(COMP1, BM_STM32G4_COMP_BLANKING << COMP_CSR_BLANKING_Pos);
    LL_COMP_Enable(COMP1);

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 清除过流 break 锁存（清 TIM1 SR.BIF，解除 MOE 硬件锁死）。
 *
 * 前置条件：过流源已消失（COMP 输出回到非触发态），否则清标志无效、
 * MOE 仍无法重武装（上层须先排除故障再调用，随后 enable_outputs 恢复输出）。
 *
 * @param dev HAL 设备实例。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int bm_vendor_comp_clear_latch(const struct bm_hal_comp *dev)
{
    bm_vendor_comp_context_t *ctx;

    ctx = bm_vendor_comp_context_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_comp_hw_init(ctx) != BM_OK) {
        return BM_ERR_INVALID;
    }
    LL_TIM_ClearFlag_BRK(TIM1);
    return BM_OK;
}

/** @brief 比较器 HAL 驱动 API 表。 */
static const struct bm_comp_driver_api g_comp_api = {
    bm_vendor_comp_clear_latch,
};

/** @brief M0 电机过流比较器实例。 */
const bm_hal_comp_t bm_hal_comp_m0 = { &g_comp_api, &g_comp_config_m0 };

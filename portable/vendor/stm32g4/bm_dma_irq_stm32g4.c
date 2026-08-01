/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_dma_irq_stm32g4.c
 * @brief STM32G4 DMA 通道中断统一路由器实现
 * @maturity E1
 *
 * 每个 DMA 通道向量唯一 Handler；驱动经 bm_dma_irq_register 挂接。
 * 未注册通道触发时清除 GIF/TCIF/HTIF/TEIF，避免中断风暴。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 DMA IRQ 统一路由器
 * 2026-08-01       1.0            zeh            补全中文 Doxygen 合规注释
 */
#include "bm_dma_irq_stm32g4.h"

#include "bm/common/bm_types.h"

#include <stddef.h>

#include "stm32g4xx.h"

/** @brief 每控制器通道数（G4 DMA1/DMA2 均为 8）。 */
#define BM_DMA_IRQ_CH_COUNT  8u
/** @brief 控制器数量（DMA1 + DMA2）。 */
#define BM_DMA_IRQ_CTRL_COUNT  2u

/** @brief 单通道注册槽。 */
typedef struct {
    bm_dma_irq_handler_t handler;
    void                *ctx;
} bm_dma_irq_slot_t;

/** @brief [ctrl_index][ch_index]，ctrl_index=0→DMA1，ch_index=0→CH1。 */
static bm_dma_irq_slot_t s_slots[BM_DMA_IRQ_CTRL_COUNT][BM_DMA_IRQ_CH_COUNT];

/**
 * @brief 校验并换算槽位下标。
 *
 * @return BM_OK；非法返回 BM_ERR_INVALID
 */
static int bm_dma_irq_index(uint8_t ctrl, uint8_t ch,
                            uint8_t *out_ci, uint8_t *out_chi) {
    if (ctrl < 1u || ctrl > BM_DMA_IRQ_CTRL_COUNT ||
        ch < 1u || ch > BM_DMA_IRQ_CH_COUNT ||
        out_ci == NULL || out_chi == NULL) {
        return BM_ERR_INVALID;
    }
    *out_ci = (uint8_t)(ctrl - 1u);
    *out_chi = (uint8_t)(ch - 1u);
    return BM_OK;
}

/**
 * @brief 清除指定通道全部 ISR 标志（每通道 4 bit）。
 */
static void bm_dma_irq_clear_flags(uint8_t ctrl, uint8_t ch) {
    DMA_TypeDef *dma = (ctrl == 1u) ? DMA1 : DMA2;
    uint32_t mask = 0xFu << ((uint32_t)(ch - 1u) * 4u);

    dma->IFCR = mask;
}

/**
 * @brief 分发到已注册回调；未注册则清标志。
 */
static void bm_dma_irq_dispatch(uint8_t ctrl, uint8_t ch) {
    uint8_t ci;
    uint8_t chi;
    bm_dma_irq_handler_t handler;
    void *ctx;

    if (bm_dma_irq_index(ctrl, ch, &ci, &chi) != BM_OK) {
        return;
    }

    handler = s_slots[ci][chi].handler;
    ctx = s_slots[ci][chi].ctx;
    if (handler != NULL) {
        handler(ctx);
    } else {
        bm_dma_irq_clear_flags(ctrl, ch);
    }
}

int bm_dma_irq_register(uint8_t ctrl, uint8_t ch,
                        bm_dma_irq_handler_t handler, void *ctx) {
    uint8_t ci;
    uint8_t chi;
    bm_dma_irq_slot_t *slot;

    if (handler == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_dma_irq_index(ctrl, ch, &ci, &chi) != BM_OK) {
        return BM_ERR_INVALID;
    }

    slot = &s_slots[ci][chi];
    if (slot->handler != NULL &&
        (slot->handler != handler || slot->ctx != ctx)) {
        return BM_ERR_BUSY;
    }

    slot->handler = handler;
    slot->ctx = ctx;
    return BM_OK;
}

void bm_dma_irq_unregister(uint8_t ctrl, uint8_t ch) {
    uint8_t ci;
    uint8_t chi;

    if (bm_dma_irq_index(ctrl, ch, &ci, &chi) != BM_OK) {
        return;
    }
    s_slots[ci][chi].handler = NULL;
    s_slots[ci][chi].ctx = NULL;
}

/* -------------------------------------------------------------------------- */
/*  唯一 DMA 向量 Handler 集                                                   */
/* -------------------------------------------------------------------------- */

void DMA1_Channel1_IRQHandler(void) { bm_dma_irq_dispatch(1u, 1u); }
void DMA1_Channel2_IRQHandler(void) { bm_dma_irq_dispatch(1u, 2u); }
void DMA1_Channel3_IRQHandler(void) { bm_dma_irq_dispatch(1u, 3u); }
void DMA1_Channel4_IRQHandler(void) { bm_dma_irq_dispatch(1u, 4u); }
void DMA1_Channel5_IRQHandler(void) { bm_dma_irq_dispatch(1u, 5u); }
void DMA1_Channel6_IRQHandler(void) { bm_dma_irq_dispatch(1u, 6u); }
void DMA1_Channel7_IRQHandler(void) { bm_dma_irq_dispatch(1u, 7u); }
void DMA1_Channel8_IRQHandler(void) { bm_dma_irq_dispatch(1u, 8u); }

void DMA2_Channel1_IRQHandler(void) { bm_dma_irq_dispatch(2u, 1u); }
void DMA2_Channel2_IRQHandler(void) { bm_dma_irq_dispatch(2u, 2u); }
void DMA2_Channel3_IRQHandler(void) { bm_dma_irq_dispatch(2u, 3u); }
void DMA2_Channel4_IRQHandler(void) { bm_dma_irq_dispatch(2u, 4u); }
void DMA2_Channel5_IRQHandler(void) { bm_dma_irq_dispatch(2u, 5u); }
void DMA2_Channel6_IRQHandler(void) { bm_dma_irq_dispatch(2u, 6u); }
void DMA2_Channel7_IRQHandler(void) { bm_dma_irq_dispatch(2u, 7u); }
void DMA2_Channel8_IRQHandler(void) { bm_dma_irq_dispatch(2u, 8u); }

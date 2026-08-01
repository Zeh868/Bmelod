/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_dma_usart2_rx_stm32g4.c
 * @brief STM32G474xB USART2 RX DMA 块流驱动（bm_drv_dma_stream 契约，STM32 LL 库）
 * @maturity E1
 *
 * DMA1 循环模式：submit_rx 把 bm_block_t 的 data/capacity 作为环形接收
 * 缓冲，半满触发 binding.on_half、全满触发 binding.on_full（ISR 上下文，
 * FPU 守卫包裹），与 bm_stream 的 DMA_OWNED ↔ READY 所有权交接配套
 * （语义见 bm_hal_dma_stream.h 与 portable/sim/native 的参考实现）。
 *
 * 通道/请求号走 bm_hal_instances_stm32g4.h 宏（默认 DMA1_CH3，
 * DMAMUX 请求 USART2_RX=26）。UART TX DMA 未实现（登记缺口）。
 *
 * 保留 CMSIS 写法的位置：NVIC（LL 无抽象）与 DMA ISR/IFCR 按通道号标志
 * （LL 仅提供按通道的 TC1..8 逐个函数，无通用 API），逐处注释。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（UART RX DMA）
 * 2026-07-28       1.1            zeh            DMA IRQ 改 bm_dma_irq 路由器注册
 *
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_dma_usart2_rx_stm32g4.h"
#include "bm_dma_irq_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"
#include "armv7em/bm_arch_isr_fpu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_dmamux.h"
#include "stm32g4xx_ll_usart.h"

/** @brief RX DMA 通道 LL 索引（0-based = 通道号-1）。 */
#define BM_VENDOR_U2RX_DMA_CH  (BM_STM32G4_USART2_RX_DMA_CH - 1u)
/** @brief DMA 半满标志位（ISR bit 4×ch+2，ch 为 0-based 索引）。 */
#define BM_VENDOR_DMA_HT_FLAG(ch)  (1u << ((ch) * 4u + 2u))
/** @brief DMA 全满标志位（ISR bit 4×ch+1）。 */
#define BM_VENDOR_DMA_TC_FLAG(ch)  (1u << ((ch) * 4u + 1u))

/** @brief USART2 RX DMA 路由入口（定义见文件后部）。 */
static void bm_vendor_u2rx_dma_irq_entry(void *user);

typedef struct {
    /** @brief 实例编号（0=USART2 RX）。 */
    uint32_t id;
} bm_vendor_u2rx_dma_config_t;

typedef struct {
    /** @brief RX 半满/全满绑定。 */
    struct bm_hal_dma_stream_binding binding;
    /** @brief 设备指针（回调首参透传）。 */
    const struct bm_hal_dma_stream *stream;
    /** @brief 进行中的接收块（NULL=空闲）。 */
    bm_block_t *active_block;
    /** @brief ISR FPU 现场保存区占位（armv7em 上守卫 no-op）。 */
    uint8_t fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));
} bm_vendor_u2rx_dma_context_t;

/** @brief USART2 RX DMA 上下文。 */
static bm_vendor_u2rx_dma_context_t g_u2rx_ctx;
/** @brief 静态配置。 */
static const bm_vendor_u2rx_dma_config_t g_u2rx_config = { 0u };

/**
 * @brief 提取上下文（单实例，config 为空或编号 0 均合法）。
 */
static bm_vendor_u2rx_dma_context_t *bm_vendor_u2rx_context_for(
    const struct bm_hal_dma_stream *dev)
{
    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    return &g_u2rx_ctx;
}

/**
 * @brief 绑定 RX 半满/全满回调；NULL = 先关中断源再清回调。
 */
static int bm_vendor_u2rx_bind_rx(const struct bm_hal_dma_stream *dev,
                                  const struct bm_hal_dma_stream_binding *binding)
{
    bm_vendor_u2rx_dma_context_t *ctx = bm_vendor_u2rx_context_for(dev);

    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    if (binding == NULL) {
        LL_DMA_DisableIT_HT(DMA1, BM_VENDOR_U2RX_DMA_CH);
        LL_DMA_DisableIT_TC(DMA1, BM_VENDOR_U2RX_DMA_CH);
        NVIC_DisableIRQ((IRQn_Type)(DMA1_Channel1_IRQn
                                    + (int)BM_VENDOR_U2RX_DMA_CH));
        bm_dma_irq_unregister(1u, (uint8_t)BM_STM32G4_USART2_RX_DMA_CH);
        memset(&ctx->binding, 0, sizeof(ctx->binding));
        return BM_OK;
    }
    if (bm_dma_irq_register(1u, (uint8_t)BM_STM32G4_USART2_RX_DMA_CH,
                            bm_vendor_u2rx_dma_irq_entry, NULL) != BM_OK) {
        return BM_ERR_BUSY;
    }
    ctx->binding = *binding;
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1
                             | LL_AHB1_GRP1_PERIPH_DMAMUX1);
    NVIC_SetPriority((IRQn_Type)(DMA1_Channel1_IRQn
                                 + (int)BM_VENDOR_U2RX_DMA_CH),
                     BM_STM32G4_USART2_RX_DMA_IRQ_PRIORITY);
    NVIC_EnableIRQ((IRQn_Type)(DMA1_Channel1_IRQn
                               + (int)BM_VENDOR_U2RX_DMA_CH));
    return BM_OK;
}

/**
 * @brief 提交 RX 块（块 data/capacity 作为环形接收缓冲，启动 DMA）。
 */
static int bm_vendor_u2rx_submit_rx(const struct bm_hal_dma_stream *dev,
                                    bm_block_t *block)
{
    bm_vendor_u2rx_dma_context_t *ctx = bm_vendor_u2rx_context_for(dev);

    if (ctx == NULL || block == NULL || block->data == NULL
        || block->capacity_bytes == 0u) {
        return BM_ERR_INVALID;
    }
    if (ctx->binding.on_half == NULL && ctx->binding.on_full == NULL) {
        return BM_ERR_NOT_INIT;
    }
    if (ctx->active_block != NULL) {
        return BM_ERR_BUSY;
    }
    ctx->active_block = block;
    ctx->stream       = dev;

    LL_DMA_DisableChannel(DMA1, BM_VENDOR_U2RX_DMA_CH);
    LL_DMAMUX_SetRequestID(DMAMUX1, BM_VENDOR_U2RX_DMA_CH,
                           BM_STM32G4_USART2_RX_DMA_REQ);
    LL_DMA_SetPeriphAddress(DMA1, BM_VENDOR_U2RX_DMA_CH,
                            (uint32_t)&USART2->RDR);
    LL_DMA_SetMemoryAddress(DMA1, BM_VENDOR_U2RX_DMA_CH,
                            (uint32_t)block->data);
    LL_DMA_SetDataLength(DMA1, BM_VENDOR_U2RX_DMA_CH,
                         block->capacity_bytes);
    LL_DMA_ConfigTransfer(DMA1, BM_VENDOR_U2RX_DMA_CH,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_CIRCULAR
        | LL_DMA_PERIPH_NOINCREMENT | LL_DMA_MEMORY_INCREMENT
        | LL_DMA_PDATAALIGN_BYTE | LL_DMA_MDATAALIGN_BYTE
        | LL_DMA_PRIORITY_HIGH);
    /* 清挂起标志（LL 无按通道号通用清标志 API，写 IFCR） */
    DMA1->IFCR = 0xFu << (BM_VENDOR_U2RX_DMA_CH * 4u);
    LL_DMA_EnableIT_HT(DMA1, BM_VENDOR_U2RX_DMA_CH);
    LL_DMA_EnableIT_TC(DMA1, BM_VENDOR_U2RX_DMA_CH);
    LL_DMA_EnableChannel(DMA1, BM_VENDOR_U2RX_DMA_CH);
    LL_USART_EnableDMAReq_RX(USART2);
    return BM_OK;
}

/**
 * @brief 中止 RX：停 DMA，返回未交付块（valid_bytes 折算已收字节数）。
 */
static bm_block_t *bm_vendor_u2rx_detach_rx(const struct bm_hal_dma_stream *dev)
{
    bm_vendor_u2rx_dma_context_t *ctx = bm_vendor_u2rx_context_for(dev);
    bm_block_t *block;

    if (ctx == NULL) {
        return NULL;
    }
    LL_USART_DisableDMAReq_RX(USART2);
    LL_DMA_DisableChannel(DMA1, BM_VENDOR_U2RX_DMA_CH);
    block = ctx->active_block;
    if (block != NULL) {
        /* 已收 = capacity - 剩余（LL_DMA_GetDataLength 读 NDTR） */
        block->valid_bytes = block->capacity_bytes
                             - LL_DMA_GetDataLength(DMA1, BM_VENDOR_U2RX_DMA_CH);
    }
    ctx->active_block = NULL;
    return block;
}

/**
 * @brief USART2 RX DMA 半满/全满 ISR 公共体。
 */
static void bm_vendor_u2rx_dma_isr(uint32_t half)
{
    bm_vendor_u2rx_dma_context_t *ctx = &g_u2rx_ctx;
    unsigned fpu_prev;

    if (ctx->active_block == NULL) {
        return;
    }
    fpu_prev = bm_arch_isr_fpu_enter(ctx->fpu_sa);
    if (half != 0u) {
        ctx->active_block->valid_bytes = ctx->active_block->capacity_bytes / 2u;
        if (ctx->binding.on_half != NULL) {
            ctx->binding.on_half(ctx->stream, ctx->active_block,
                                 ctx->binding.context);
        }
    } else {
        ctx->active_block->valid_bytes = ctx->active_block->capacity_bytes;
        if (ctx->binding.on_full != NULL) {
            ctx->binding.on_full(ctx->stream, ctx->active_block,
                                 ctx->binding.context);
        }
    }
    bm_arch_isr_fpu_exit(ctx->fpu_sa, fpu_prev);
}

/**
 * @brief USART2 RX DMA 路由入口。
 */
static void bm_vendor_u2rx_dma_irq_entry(void *user)
{
    (void)user;
    if ((DMA1->ISR & BM_VENDOR_DMA_HT_FLAG(BM_VENDOR_U2RX_DMA_CH)) != 0u) {
        DMA1->IFCR = BM_VENDOR_DMA_HT_FLAG(BM_VENDOR_U2RX_DMA_CH);
        bm_vendor_u2rx_dma_isr(1u);
    }
    if ((DMA1->ISR & BM_VENDOR_DMA_TC_FLAG(BM_VENDOR_U2RX_DMA_CH)) != 0u) {
        DMA1->IFCR = BM_VENDOR_DMA_TC_FLAG(BM_VENDOR_U2RX_DMA_CH);
        bm_vendor_u2rx_dma_isr(0u);
    }
}

/** @brief USART2 RX DMA 块流驱动 API 表。 */
static const struct bm_dma_stream_driver_api g_u2rx_dma_api = {
    bm_vendor_u2rx_bind_rx,
    bm_vendor_u2rx_submit_rx,
    bm_vendor_u2rx_detach_rx,
};

/** @brief USART2 RX DMA 块流设备。 */
const bm_hal_dma_stream_t bm_stm32g4_usart2_rx_dma = {
    &g_u2rx_dma_api, &g_u2rx_config,
};

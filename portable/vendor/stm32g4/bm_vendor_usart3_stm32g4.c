/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_usart3_stm32g4.c
 * @brief STM32G4 USART3 设备实例驱动（IDLE + DMA TX/RX）
 *
 * App 通过 `bm_usart3_stm32g4_config_t` 指定引脚/DMA/IRQ/DE；Bmelod 不固定 USART3。
 * RX 走 DMA 循环模式 + USART IDLE 中断；收到 IDLE 后停止 DMA、计算已接收字节数、
 * 调用 rx_frame_callback，再重启 DMA。TX 走 DMA 正常模式 + TC 中断，TC 时调用
 * tx_complete_callback。
 *
 * ISR 有界：只清标志、操作 DMA/USART 寄存器、派发回调，不解析业务协议。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 USART3 后端
 */
#include "bm_vendor_usart3_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_dmamux.h"
#include "stm32g4xx_ll_usart.h"

/** @brief 默认 USART3 配置。 */
static const bm_usart3_stm32g4_config_t g_usart3_cfg_default = {
    .baud = BM_STM32G4_USART3_BAUD,
    .parity = BM_UART_PARITY_NONE,
    .stop_bits = BM_UART_STOPBITS_1,
    .data_bits = BM_UART_DATABITS_8,
    .tx_pin = BM_STM32G4_USART3_TX_PIN,
    .rx_pin = BM_STM32G4_USART3_RX_PIN,
    .gpio_af = BM_STM32G4_USART3_GPIO_AF,
    .tx_dma_ch = BM_STM32G4_USART3_TX_DMA_CH,
    .tx_dma_req = BM_STM32G4_USART3_TX_DMA_REQ,
    .rx_dma_ch = BM_STM32G4_USART3_RX_DMA_CH,
    .rx_dma_req = BM_STM32G4_USART3_RX_DMA_REQ,
    .usart_irqn = USART3_IRQn,
    .tx_dma_irqn = DMA1_Channel4_IRQn, /* 默认 DMA1_CH4 */
    .rx_dma_irqn = DMA1_Channel5_IRQn, /* 默认 DMA1_CH5 */
    .irq_priority = BM_STM32G4_USART3_IRQ_PRIORITY,
    .tx_dma_irq_priority = BM_STM32G4_USART3_TX_DMA_IRQ_PRIORITY,
    .rx_dma_irq_priority = BM_STM32G4_USART3_RX_DMA_IRQ_PRIORITY,
};

/** @brief 运行时上下文。 */
typedef struct {
    const bm_usart3_stm32g4_config_t *cfg;
    uint8_t                          *rx_buf;
    size_t                            rx_len;
    int                               initialized;
    int                               tx_busy;
    bm_uart_tx_complete_callback_t    tx_complete_cb;
    void                             *tx_complete_user;
    bm_uart_rx_frame_callback_t       rx_frame_cb;
    void                             *rx_frame_user;
    bm_uart_stats_t                   stats;
} bm_usart3_context_t;

static bm_usart3_context_t g_usart3_ctx;

/* -------------------------------------------------------------------------- */
/*  底层辅助                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 由通道号（1-based）取 DMA 通道寄存器指针。
 */
static DMA_Channel_TypeDef *bm_usart3_dma_ch(uint32_t ch) {
    switch (ch) {
    case 1u: return DMA1_Channel1;
    case 2u: return DMA1_Channel2;
    case 3u: return DMA1_Channel3;
    case 4u: return DMA1_Channel4;
    case 5u: return DMA1_Channel5;
    case 6u: return DMA1_Channel6;
    case 7u: return DMA1_Channel7;
    default: return NULL;
    }
}

/**
 * @brief GPIO 复用配置（推挽、高速、无上下拉）。
 */
static void bm_usart3_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af) {
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
 * @brief 更新错误统计。
 */
static void bm_usart3_update_errors(bm_usart3_context_t *ctx) {
    uint32_t isr = USART3->ISR;
    uint32_t errs = 0u;

    if ((isr & USART_ISR_ORE) != 0u) {
        errs |= BM_UART_ERR_OVERRUN;
        ctx->stats.rx_overrun_count++;
        LL_USART_ClearFlag_ORE(USART3);
    }
    if ((isr & USART_ISR_FE) != 0u) {
        errs |= BM_UART_ERR_FRAMING;
        ctx->stats.rx_framing_count++;
        LL_USART_ClearFlag_FE(USART3);
    }
    if ((isr & USART_ISR_PE) != 0u) {
        errs |= BM_UART_ERR_PARITY;
        ctx->stats.rx_parity_count++;
        LL_USART_ClearFlag_PE(USART3);
    }
    if ((isr & USART_ISR_NE) != 0u) {
        errs |= BM_UART_ERR_NOISE;
        ctx->stats.rx_noise_count++;
        LL_USART_ClearFlag_NE(USART3);
    }
    if (errs != 0u) {
        ctx->stats.last_errors |= errs;
    }
}

/**
 * @brief 启动 RX DMA 循环接收。
 */
static void bm_usart3_rx_dma_start(bm_usart3_context_t *ctx) {
    DMA_Channel_TypeDef *dma = bm_usart3_dma_ch(ctx->cfg->rx_dma_ch);

    if (dma == NULL || ctx->rx_buf == NULL || ctx->rx_len == 0u) {
        return;
    }

    LL_DMA_DisableChannel(dma);
    LL_DMA_SetMemoryAddress(dma, (uint32_t)ctx->rx_buf);
    LL_DMA_SetPeriphAddress(dma, (uint32_t)&USART3->RDR);
    LL_DMA_SetDataLength(dma, (uint32_t)ctx->rx_len);
    LL_DMA_SetMode(dma, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(dma, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(dma, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(dma, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(dma, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetDataTransferDirection(dma, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetChannelPriorityLevel(dma, LL_DMA_PRIORITY_MEDIUM);
    LL_DMA_EnableChannel(dma);
    LL_USART_EnableDMAReq_RX(USART3);
}

/**
 * @brief 停止 RX DMA 并返回已接收字节数（从 DMA 剩余计数）。
 */
static size_t bm_usart3_rx_dma_received(bm_usart3_context_t *ctx) {
    DMA_Channel_TypeDef *dma = bm_usart3_dma_ch(ctx->cfg->rx_dma_ch);
    size_t remaining;

    if (dma == NULL || ctx->rx_len == 0u) {
        return 0u;
    }
    remaining = (size_t)LL_DMA_GetDataLength(dma);
    if (remaining > ctx->rx_len) {
        return 0u;
    }
    return ctx->rx_len - remaining;
}

/* -------------------------------------------------------------------------- */
/*  ISR                                                                       */
/* -------------------------------------------------------------------------- */

void USART3_IRQHandler(void) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    size_t received;

    if (ctx->cfg == NULL) {
        return;
    }

    bm_usart3_update_errors(ctx);

    if (LL_USART_IsActiveFlag_IDLE(USART3) != 0u) {
        LL_USART_ClearFlag_IDLE(USART3);

        received = bm_usart3_rx_dma_received(ctx);
        if (received > 0u && ctx->rx_frame_cb != NULL) {
            ctx->rx_frame_cb((const bm_hal_uart_t *)ctx,
                             BM_UART_EVT_IDLE, received,
                             ctx->rx_frame_user);
        }
    }
}

void DMA1_Channel4_IRQHandler(void) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    DMA_Channel_TypeDef *dma = DMA1_Channel4;

    if (ctx->cfg == NULL || ctx->cfg->tx_dma_ch != 4u) {
        return;
    }
    if (LL_DMA_IsActiveFlag_TC4(dma) != 0u) {
        LL_DMA_ClearFlag_TC4(dma);
        LL_DMA_DisableChannel(dma);
        LL_USART_DisableDMAReq_TX(USART3);
        ctx->tx_busy = 0;
        /* 等待 UART TC，确保总线上最后一位已发出 */
        while (LL_USART_IsActiveFlag_TC(USART3) == 0u) {
        }
        if (ctx->tx_complete_cb != NULL) {
            ctx->tx_complete_cb((const bm_hal_uart_t *)ctx,
                                ctx->tx_complete_user);
        }
    }
}

void DMA1_Channel5_IRQHandler(void) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    DMA_Channel_TypeDef *dma = DMA1_Channel5;

    if (ctx->cfg == NULL || ctx->cfg->rx_dma_ch != 5u) {
        return;
    }
    if (LL_DMA_IsActiveFlag_TC5(dma) != 0u) {
        LL_DMA_ClearFlag_TC5(dma);
    }
    if (LL_DMA_IsActiveFlag_HT5(dma) != 0u) {
        LL_DMA_ClearFlag_HT5(dma);
    }
}

/* -------------------------------------------------------------------------- */
/*  HAL API 实现                                                               */
/* -------------------------------------------------------------------------- */

static int bm_vendor_usart3_init(const struct bm_hal_uart *dev, void *config) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    const bm_usart3_stm32g4_config_t *runtime_cfg =
        (const bm_usart3_stm32g4_config_t *)config;
    LL_RCC_ClocksTypeDef clocks;
    uint32_t parity_ll;
    uint32_t stop_ll;
    uint32_t data_bits;

    (void)dev;

    ctx->cfg = (runtime_cfg != NULL) ? runtime_cfg : &g_usart3_cfg_default;
    ctx->initialized = 1;
    ctx->tx_busy = 0;
    (void)memset(&ctx->stats, 0, sizeof(ctx->stats));

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);

    bm_usart3_gpio_af(GPIOB, ctx->cfg->tx_pin, ctx->cfg->gpio_af);
    bm_usart3_gpio_af(GPIOB, ctx->cfg->rx_pin, ctx->cfg->gpio_af);

    /* DMAMUX 请求映射 */
    LL_DMA_SetPeriphRequest(DMA1_Channel4, ctx->cfg->tx_dma_req);
    LL_DMA_SetPeriphRequest(DMA1_Channel5, ctx->cfg->rx_dma_req);

    LL_USART_Disable(USART3);

    switch (ctx->cfg->parity) {
    case BM_UART_PARITY_EVEN:
        parity_ll = LL_USART_PARITY_EVEN;
        break;
    case BM_UART_PARITY_ODD:
        parity_ll = LL_USART_PARITY_ODD;
        break;
    default:
        parity_ll = LL_USART_PARITY_NONE;
        break;
    }

    stop_ll = (ctx->cfg->stop_bits == BM_UART_STOPBITS_2)
                  ? LL_USART_STOPBITS_2
                  : LL_USART_STOPBITS_1;

    data_bits = (ctx->cfg->data_bits == BM_UART_DATABITS_9) ? 9u : 8u;
    if (ctx->cfg->parity != BM_UART_PARITY_NONE) {
        data_bits = (data_bits == 9u) ? 9u : 8u; /* M 位与校验组合简化 */
    }

    LL_RCC_GetSystemClocksFreq(&clocks);
    LL_USART_ConfigAsyncMode(USART3);
    LL_USART_SetBaudRate(USART3, clocks.PCLK1_Frequency,
                         LL_USART_PRESCALER_DIV1,
                         LL_USART_OVERSAMPLING_16,
                         ctx->cfg->baud);
    LL_USART_SetDataWidth(USART3,
        (data_bits == 9u) ? LL_USART_DATAWIDTH_9B : LL_USART_DATAWIDTH_8B);
    LL_USART_SetParity(USART3, parity_ll);
    LL_USART_SetStopBitsLength(USART3, stop_ll);
    LL_USART_SetTransferDirection(USART3,
                                  LL_USART_DIRECTION_TX_RX);

    LL_USART_EnableIT_IDLE(USART3);
    LL_USART_ClearFlag_IDLE(USART3);

    NVIC_SetPriority(ctx->cfg->usart_irqn, ctx->cfg->irq_priority);
    NVIC_EnableIRQ(ctx->cfg->usart_irqn);

    LL_USART_Enable(USART3);

    /* 若已配置 ring buffer，立即启动 RX DMA */
    if (ctx->rx_buf != NULL && ctx->rx_len != 0u) {
        bm_usart3_rx_dma_start(ctx);
    }

    return BM_OK;
}

static int bm_vendor_usart3_send(const struct bm_hal_uart *dev,
                                 const uint8_t *data, size_t len) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    DMA_Channel_TypeDef *dma;

    (void)dev;
    if (ctx->initialized == 0 || data == NULL || len == 0u) {
        return BM_ERR_INVALID;
    }
    if (ctx->tx_busy != 0) {
        return BM_ERR_BUSY;
    }

    dma = bm_usart3_dma_ch(ctx->cfg->tx_dma_ch);
    if (dma == NULL) {
        return BM_ERR_INVALID;
    }

    ctx->tx_busy = 1;
    ctx->stats.tx_count += (uint32_t)len;

    LL_DMA_DisableChannel(dma);
    LL_DMA_SetPeriphAddress(dma, (uint32_t)&USART3->TDR);
    LL_DMA_SetMemoryAddress(dma, (uint32_t)data);
    LL_DMA_SetDataLength(dma, (uint32_t)len);
    LL_DMA_SetMode(dma, LL_DMA_MODE_NORMAL);
    LL_DMA_SetPeriphIncMode(dma, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(dma, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(dma, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(dma, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetDataTransferDirection(dma, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_EnableIT_TC(dma);
    LL_DMA_EnableChannel(dma);

    NVIC_SetPriority(ctx->cfg->tx_dma_irqn, ctx->cfg->tx_dma_irq_priority);
    NVIC_EnableIRQ(ctx->cfg->tx_dma_irqn);

    LL_USART_EnableDMAReq_TX(USART3);
    return BM_OK;
}

static size_t bm_vendor_usart3_recv(const struct bm_hal_uart *dev,
                                    uint8_t *data, size_t max_len) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    size_t received;
    size_t i;

    (void)dev;
    if (ctx->initialized == 0 || data == NULL || max_len == 0u
        || ctx->rx_buf == NULL || ctx->rx_len == 0u) {
        return 0u;
    }

    received = bm_usart3_rx_dma_received(ctx);
    if (received > max_len) {
        received = max_len;
    }

    for (i = 0u; i < received; ++i) {
        data[i] = ctx->rx_buf[i];
    }
    ctx->stats.rx_count += (uint32_t)received;
    return received;
}

static void bm_vendor_usart3_set_rx_callback(const struct bm_hal_uart *dev,
                                             void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
    /* 本后端不支持单字节回调；上层应使用 set_rx_frame_callback */
}

static int bm_vendor_usart3_abort(const struct bm_hal_uart *dev) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    DMA_Channel_TypeDef *dma;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }

    dma = bm_usart3_dma_ch(ctx->cfg->tx_dma_ch);
    if (dma != NULL) {
        LL_DMA_DisableChannel(dma);
    }
    LL_USART_DisableDMAReq_TX(USART3);
    ctx->tx_busy = 0;

    dma = bm_usart3_dma_ch(ctx->cfg->rx_dma_ch);
    if (dma != NULL) {
        LL_DMA_DisableChannel(dma);
    }
    LL_USART_DisableDMAReq_RX(USART3);
    return BM_OK;
}

static int bm_vendor_usart3_flush(const struct bm_hal_uart *dev) {
    (void)dev;
    /* TX DMA 完成中断内已等待 UART TC，此处直接返回 */
    return BM_OK;
}

static int bm_vendor_usart3_set_tx_complete_callback(
    const struct bm_hal_uart *dev,
    bm_uart_tx_complete_callback_t cb, void *user) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }
    ctx->tx_complete_cb = cb;
    ctx->tx_complete_user = user;
    return BM_OK;
}

static int bm_vendor_usart3_set_rx_frame_callback(
    const struct bm_hal_uart *dev,
    bm_uart_rx_frame_callback_t cb, void *user) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }
    ctx->rx_frame_cb = cb;
    ctx->rx_frame_user = user;
    return BM_OK;
}

static int bm_vendor_usart3_set_rx_buffer(const struct bm_hal_uart *dev,
                                          uint8_t *buf, size_t len) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }
    if (buf == NULL || len == 0u) {
        ctx->rx_buf = NULL;
        ctx->rx_len = 0u;
        return BM_ERR_INVALID;
    }
    ctx->rx_buf = buf;
    ctx->rx_len = len;
    bm_usart3_rx_dma_start(ctx);
    return BM_OK;
}

static int bm_vendor_usart3_get_stats(const struct bm_hal_uart *dev,
                                      bm_uart_stats_t *stats) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;

    (void)dev;
    if (ctx->initialized == 0 || stats == NULL) {
        return BM_ERR_INVALID;
    }
    *stats = ctx->stats;
    ctx->stats.last_errors = 0u;
    return BM_OK;
}

static const struct bm_uart_driver_api g_usart3_api = {
    bm_vendor_usart3_init,
    bm_vendor_usart3_send,
    bm_vendor_usart3_recv,
    bm_vendor_usart3_set_rx_callback,
    bm_vendor_usart3_abort,
    bm_vendor_usart3_flush,
    bm_vendor_usart3_set_tx_complete_callback,
    bm_vendor_usart3_set_rx_frame_callback,
    bm_vendor_usart3_set_rx_buffer,
    bm_vendor_usart3_get_stats,
};

const bm_hal_uart_t bm_stm32g4_usart3 = { &g_usart3_api,
                                          &g_usart3_cfg_default };

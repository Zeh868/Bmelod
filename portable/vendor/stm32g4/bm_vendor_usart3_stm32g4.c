/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_usart3_stm32g4.c
 * @brief STM32G4 USART3 设备实例驱动（IDLE + DMA TX/RX）
 * @maturity E1
 *
 * App 通过 `bm_usart3_stm32g4_config_t` 指定端口/Pin/AF/DMA/IRQ；Bmelod 不固定
 * USART3 与具体产品引脚。
 *
 * 设计要点：
 * - TX/RX GPIO 端口、引脚、AF 全部由配置指定，后端不写死 GPIOB。
 * - TX/RX DMA 控制器（DMA1/DMA2）与通道号全部由配置指定；IRQ 经
 *   `bm_dma_irq_stm32g4` 路由器注册，运行时改通道无需再导出 Handler。
 * - RX 使用 DMA 循环模式 + 软件读指针：IDLE/RX_FULL 交付完整帧事件；HT 仅
 *   维护写指针与溢出检测，不标 FRAME_END（避免长帧半缓冲误拆）。
 * - TX 使用 DMA 正常模式，DMA TC 后开启 USART TC 中断，在 USART TC 到达时才
 *   触发发送完成回调，确保最后一个停止位已离开发送器。
 * - TX DMA TE：停 DMA、清 TC IT、结束 TX 会话并调用 tx_complete_cb，供 RS485
 *   等上层立即退出发送态。
 * - 回调统一传递真实的 `bm_hal_uart_t *dev` 指针，不把 context 强转成设备。
 * - init 对引脚、DMA、IRQ、波特率/数据位/校验/停止位做合法性校验；失败时回滚
 *   已配置的 NVIC/时钟。kernel_clock_hz==0 时假定 USART 时钟=PCLK1。
 *
 * @author zeh (china_qzh@163.com)
 * @version 3.5
 * @date 2026-07-31
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 USART3 后端
 * 2026-07-28       2.0            zeh            支持任意端口/DMA 控制器/通道；
 *                                                重写环形 RX 与两段式 TX 完成
 * 2026-07-28       3.0            zeh            DMA IRQ handler 按配置条件定义；
 *                                                validate 改纯算术校验（不读 BRR）；
 *                                                DMA TC 标志修正为 TCIF(+1)，RX ISR
 *                                                补 TEIF 检查，DMA TE 改记 OVERRUN
 * 2026-07-28       3.1            zeh            DMA IRQ 改路由器注册；TX TE 结束
 *                                                会话并回调上层
 * 2026-07-28       3.2            zeh            HT 不再交付 FRAME_END；支持
 *                                                kernel_clock_hz
 * 2026-07-29       3.3            zeh            RX 环形缓冲改用单调 produced/consumed
 *                                                计数，消除整圈丢失风险
 * 2026-07-29       3.4            zeh            TC ISR 用 produced_before 计算整圈新增量，
 *                                                修正 HT→TC 后一半字节统计与溢出判断
 * 2026-07-31       3.5            zeh            RX ring 的 produced/consumed 读写序列加全局
 *                                                bm_critical_enter 互斥（rx_update 记账段、
 *                                                TC mark_full_round 段、recv update+consume 段），
 *                                                修复 IDLE/DMA ISR 与 SRT recv 多上下文抢占
 *                                                导致 produced 回退、pending 下溢回绕、
 *                                                数据重复/错乱交付的竞争；回调均在锁外
 * 2026-08-01       3.5            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_usart3_stm32g4.h"
#include "bm_dma_irq_stm32g4.h"
#include "bm_dma_circular_ring.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_critical_wrap.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_dmamux.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_usart.h"

/** @brief 默认 USART3 配置（PB10/PB11，DMA1_CH4/CH5，115200 8N1）。 */
static const bm_usart3_stm32g4_config_t g_usart3_cfg_default = {
    .baud = BM_STM32G4_USART3_BAUD,
    .parity = BM_UART_PARITY_NONE,
    .stop_bits = BM_UART_STOPBITS_1,
    .data_bits = BM_UART_DATABITS_8,
    .kernel_clock_hz = 0u,
    .tx_port = BM_STM32G4_USART3_TX_PORT,
    .tx_pin = BM_STM32G4_USART3_TX_PIN,
    .rx_port = BM_STM32G4_USART3_RX_PORT,
    .rx_pin = BM_STM32G4_USART3_RX_PIN,
    .gpio_af = BM_STM32G4_USART3_GPIO_AF,
    .tx_dma_ctrl = BM_STM32G4_USART3_TX_DMA_CTRL,
    .tx_dma_ch = BM_STM32G4_USART3_TX_DMA_CH,
    .tx_dma_req = BM_STM32G4_USART3_TX_DMA_REQ,
    .rx_dma_ctrl = BM_STM32G4_USART3_RX_DMA_CTRL,
    .rx_dma_ch = BM_STM32G4_USART3_RX_DMA_CH,
    .rx_dma_req = BM_STM32G4_USART3_RX_DMA_REQ,
    .usart_irqn = USART3_IRQn,
    .tx_dma_irqn = DMA1_Channel4_IRQn,
    .rx_dma_irqn = DMA1_Channel5_IRQn,
    .irq_priority = BM_STM32G4_USART3_IRQ_PRIORITY,
    .tx_dma_irq_priority = BM_STM32G4_USART3_TX_DMA_IRQ_PRIORITY,
    .rx_dma_irq_priority = BM_STM32G4_USART3_RX_DMA_IRQ_PRIORITY,
};

/** @brief 运行时上下文。 */
typedef struct {
    const bm_hal_uart_t              *dev;
    const bm_usart3_stm32g4_config_t *cfg;
    int                                initialized;
    int                                tx_busy;

    uint8_t                           *rx_buf;
    size_t                             rx_len;
    bm_dma_circ_ring_t                 rx_ring;      /**< DMA 循环接收记账 */

    bm_uart_tx_complete_callback_t     tx_complete_cb;
    void                              *tx_complete_user;
    bm_uart_rx_frame_callback_t        rx_frame_cb;
    void                              *rx_frame_user;

    bm_uart_stats_t                    stats;
} bm_usart3_context_t;

static bm_usart3_context_t g_usart3_ctx;

/* -------------------------------------------------------------------------- */
/*  底层辅助                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 由端口编码取 GPIO 寄存器指针。
 */
static GPIO_TypeDef *bm_usart3_port(uint32_t port) {
    switch (port) {
    case 0u: return GPIOA;
    case 1u: return GPIOB;
    case 2u: return GPIOC;
    case 3u: return GPIOD;
    case 4u: return GPIOE;
    case 5u: return GPIOF;
    case 6u: return GPIOG;
    default: return NULL;
    }
}

/**
 * @brief 由 DMA 控制器号取 DMA 寄存器指针。
 */
static DMA_TypeDef *bm_usart3_dma_ctrl(uint32_t ctrl) {
    return (ctrl == 2u) ? DMA2 : DMA1;
}

/**
 * @brief 使能指定 GPIO 端口时钟。
 */
static void bm_usart3_gpio_clock_enable(uint32_t port) {
    switch (port) {
    case 0u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA); break;
    case 1u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB); break;
    case 2u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC); break;
    case 3u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOD); break;
    case 4u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOE); break;
    case 5u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOF); break;
    case 6u: LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOG); break;
    default: break;
    }
}

/**
 * @brief 使能指定 DMA 控制器时钟。
 */
static void bm_usart3_dma_clock_enable(uint32_t ctrl) {
    if (ctrl == 1u) {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    } else if (ctrl == 2u) {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
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
 * @brief 配置 TX DMA 通道（正常模式）。
 */
static void bm_usart3_tx_dma_config(const bm_usart3_context_t *ctx,
                                    DMA_TypeDef *dma, uint32_t ch,
                                    const uint8_t *data, size_t len) {
    (void)ctx;

    LL_DMA_DisableChannel(dma, ch);
    LL_DMA_SetPeriphAddress(dma, ch, (uint32_t)&USART3->TDR);
    LL_DMA_SetMemoryAddress(dma, ch, (uint32_t)data);
    LL_DMA_SetDataLength(dma, ch, (uint32_t)len);
    LL_DMA_SetMode(dma, ch, LL_DMA_MODE_NORMAL);
    LL_DMA_SetPeriphIncMode(dma, ch, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(dma, ch, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(dma, ch, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(dma, ch, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetDataTransferDirection(dma, ch, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_SetChannelPriorityLevel(dma, ch, LL_DMA_PRIORITY_MEDIUM);
}

/**
 * @brief 配置 RX DMA 通道（循环模式）。
 */
static void bm_usart3_rx_dma_config(const bm_usart3_context_t *ctx,
                                    DMA_TypeDef *dma, uint32_t ch) {
    LL_DMA_DisableChannel(dma, ch);
    LL_DMA_SetPeriphAddress(dma, ch, (uint32_t)&USART3->RDR);
    LL_DMA_SetMemoryAddress(dma, ch, (uint32_t)ctx->rx_buf);
    LL_DMA_SetDataLength(dma, ch, (uint32_t)ctx->rx_len);
    LL_DMA_SetMode(dma, ch, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(dma, ch, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(dma, ch, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(dma, ch, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(dma, ch, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetDataTransferDirection(dma, ch, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetChannelPriorityLevel(dma, ch, LL_DMA_PRIORITY_MEDIUM);
}

/**
 * @brief 启动 RX DMA 循环接收，并复位软件读/写指针。
 */
static void bm_usart3_rx_dma_start(bm_usart3_context_t *ctx) {
    DMA_TypeDef *dma;
    uint32_t ch;

    if (ctx->rx_buf == NULL || ctx->rx_len == 0u) {
        return;
    }
    if (ctx->cfg->rx_dma_ctrl < 1u || ctx->cfg->rx_dma_ctrl > 2u ||
        ctx->cfg->rx_dma_ch < 1u || ctx->cfg->rx_dma_ch > 7u) {
        return;
    }

    dma = bm_usart3_dma_ctrl(ctx->cfg->rx_dma_ctrl);
    ch = ctx->cfg->rx_dma_ch;

    bm_dma_circ_ring_reset(&ctx->rx_ring, ctx->rx_buf,
                           (uint32_t)ctx->rx_len);

    bm_usart3_rx_dma_config(ctx, dma, ch);
    LL_DMA_EnableIT_TC(dma, ch);
    LL_DMA_EnableIT_HT(dma, ch);
    LL_DMA_EnableChannel(dma, ch);
    LL_USART_EnableDMAReq_RX(USART3);
}

/**
 * @brief 停止 RX DMA。
 */
static void bm_usart3_rx_dma_stop(bm_usart3_context_t *ctx) {
    DMA_TypeDef *dma;
    uint32_t ch;

    if (ctx->cfg == NULL) {
        return;
    }
    dma = bm_usart3_dma_ctrl(ctx->cfg->rx_dma_ctrl);
    ch = ctx->cfg->rx_dma_ch;
    LL_DMA_DisableChannel(dma, ch);
    LL_USART_DisableDMAReq_RX(USART3);
}

/**
 * @brief 停止 TX DMA。
 */
static void bm_usart3_tx_dma_stop(bm_usart3_context_t *ctx) {
    DMA_TypeDef *dma;
    uint32_t ch;

    if (ctx->cfg == NULL) {
        return;
    }
    dma = bm_usart3_dma_ctrl(ctx->cfg->tx_dma_ctrl);
    ch = ctx->cfg->tx_dma_ch;
    LL_DMA_DisableChannel(dma, ch);
    LL_USART_DisableDMAReq_TX(USART3);
}

/**
 * @brief 更新 RX 写指针与溢出统计；可选通知上层帧事件。
 *
 * 使用单调 produced/consumed 计数，避免 DMA 接收整圈时 write_pos == read_pos
 * 被误判为"无数据"。
 *
 * @param ctx         运行时上下文
 * @param event       本次事件标志（notify=0 时可传 0）
 * @param notify      非零：在有未读数据时调用 rx_frame_cb；HT 须传 0，避免半缓冲误拆帧
 * @param extra_bytes 调用前已通过 mark_full_round 等方式推进 produced 但未统计的字节数；
 *                    TC 路径须传入本次完整一圈内的总新增量，普通路径传 0
 */
static void bm_usart3_rx_update(bm_usart3_context_t *ctx, uint32_t event,
                                int notify, uint32_t extra_bytes) {
    DMA_TypeDef *dma;
    uint32_t ch;
    uint32_t ndtr;
    uint32_t new_bytes;
    uint32_t total_new;
    uint32_t free_space;
    uint32_t pending;

    if (ctx->rx_buf == NULL || ctx->rx_len == 0u) {
        return;
    }

    dma = bm_usart3_dma_ctrl(ctx->cfg->rx_dma_ctrl);
    ch = ctx->cfg->rx_dma_ch;
    ndtr = LL_DMA_GetDataLength(dma, ch);
    if (ndtr > ctx->rx_len) {
        ndtr = (uint32_t)ctx->rx_len;
    }

    /* produced/consumed 的读-改-写序列必须整体互斥：本函数同时被 USART IDLE
     * ISR、RX DMA HT/TC ISR 与 SRT recv() 调用，三者优先级可不同、可无吊顶
     * 抢占。无锁时 produced 回退会使 pending = produced - consumed 下溢回绕
     * 为巨大值，consume 据此超额、重复交付。用全局 bm_critical_enter 而非
     * BM_CRITICAL_ENTER()，与板级 IRQ 优先级配置（阈值上下）解耦；
     * 窗口仅 O(1) 记账，回调留在锁外（不把用户代码圈进 IRQ-off）。
     * bm_critical_enter 可嵌套，与 TC 分支/recv 的外层锁共存无害。 */
    {
        bm_irq_state_t irq_state = bm_critical_enter();

        free_space = bm_dma_circ_ring_free_space(&ctx->rx_ring);
        /* extra_bytes 在调用前已推进 produced，当前 free_space 已扣减该部分；
         * 加回后得到事件触发前的真实剩余空间，用于正确判断溢出。 */
        if (extra_bytes != 0u) {
            free_space += extra_bytes;
        }
        new_bytes = bm_dma_circ_ring_update(&ctx->rx_ring, ndtr);
        total_new = new_bytes + extra_bytes;
        if (total_new > free_space) {
            /* 软件读取不及时导致覆盖：丢弃全部已缓冲数据 */
            ctx->stats.rx_overflow_count++;
            ctx->stats.last_errors |= BM_UART_ERR_OVERFLOW;
            bm_dma_circ_ring_drop_all(&ctx->rx_ring);
        }

        pending = bm_dma_circ_ring_pending(&ctx->rx_ring);
        ctx->stats.rx_count += total_new;
        bm_critical_exit(irq_state);
    }

    if (notify != 0 && ctx->rx_frame_cb != NULL && pending > 0u) {
        ctx->rx_frame_cb(ctx->dev, event, (size_t)pending, ctx->rx_frame_user);
    }
}

/**
 * @brief 更新 USART 错误统计并清除标志。
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
 * @brief 反初始化：关闭 NVIC 中断、DMA、USART，并注销 DMA 路由。
 */
static void bm_usart3_hw_deinit(bm_usart3_context_t *ctx) {
    if (ctx->cfg == NULL) {
        return;
    }

    NVIC_DisableIRQ(ctx->cfg->usart_irqn);
    NVIC_DisableIRQ(ctx->cfg->tx_dma_irqn);
    NVIC_DisableIRQ(ctx->cfg->rx_dma_irqn);

    bm_dma_irq_unregister((uint8_t)ctx->cfg->tx_dma_ctrl,
                          (uint8_t)ctx->cfg->tx_dma_ch);
    if (ctx->cfg->rx_dma_ctrl != ctx->cfg->tx_dma_ctrl ||
        ctx->cfg->rx_dma_ch != ctx->cfg->tx_dma_ch) {
        bm_dma_irq_unregister((uint8_t)ctx->cfg->rx_dma_ctrl,
                              (uint8_t)ctx->cfg->rx_dma_ch);
    }

    LL_USART_DisableIT_TC(USART3);
    LL_USART_DisableIT_IDLE(USART3);
    LL_USART_Disable(USART3);

    bm_usart3_tx_dma_stop(ctx);
    bm_usart3_rx_dma_stop(ctx);
}

/* -------------------------------------------------------------------------- */
/*  配置校验                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 解析 USART 内核时钟：配置非零优先，否则取 PCLK1。
 */
static uint32_t bm_usart3_kernel_hz(const bm_usart3_stm32g4_config_t *cfg) {
    LL_RCC_ClocksTypeDef clocks;

    if (cfg != NULL && cfg->kernel_clock_hz != 0u) {
        return cfg->kernel_clock_hz;
    }
    LL_RCC_GetSystemClocksFreq(&clocks);
    return clocks.PCLK1_Frequency;
}

/**
 * @brief 校验串口参数配置是否合法。
 */
static int bm_usart3_validate_config(const bm_usart3_stm32g4_config_t *cfg) {
    uint32_t ker_hz;

    if (cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (cfg->baud == 0u) {
        return BM_ERR_INVALID;
    }
    if (cfg->tx_port > 6u || cfg->tx_pin > 15u ||
        cfg->rx_port > 6u || cfg->rx_pin > 15u) {
        return BM_ERR_INVALID;
    }
    if (cfg->gpio_af > 15u) {
        return BM_ERR_INVALID;
    }
    if (cfg->tx_dma_ctrl < 1u || cfg->tx_dma_ctrl > 2u ||
        cfg->tx_dma_ch < 1u || cfg->tx_dma_ch > 7u ||
        cfg->rx_dma_ctrl < 1u || cfg->rx_dma_ctrl > 2u ||
        cfg->rx_dma_ch < 1u || cfg->rx_dma_ch > 7u) {
        return BM_ERR_INVALID;
    }
    /* DMAMUX 请求号由 App 按 RM0440 表填写；范围 0..127 */
    if (cfg->tx_dma_req > 127u || cfg->rx_dma_req > 127u) {
        return BM_ERR_INVALID;
    }
    if (cfg->parity != BM_UART_PARITY_NONE &&
        cfg->parity != BM_UART_PARITY_EVEN &&
        cfg->parity != BM_UART_PARITY_ODD) {
        return BM_ERR_INVALID;
    }
    if (cfg->stop_bits != BM_UART_STOPBITS_1 &&
        cfg->stop_bits != BM_UART_STOPBITS_2) {
        return BM_ERR_INVALID;
    }
    if (cfg->data_bits != BM_UART_DATABITS_8 &&
        cfg->data_bits != BM_UART_DATABITS_9) {
        return BM_ERR_INVALID;
    }

    /* 纯算术校验波特率可达性：OVERSAMPLING_16 要求 USARTDIV = ker/baud >= 16。
     * 不得在此读 LL_USART_GetBaudRate——validate 先于时钟使能与 SetBaudRate
     * 执行，BRR 复位值为 0 会导致恒定误判；硬件回读检查保留在 init 中。 */
    ker_hz = bm_usart3_kernel_hz(cfg);
    if ((ker_hz / 16u) < cfg->baud) {
        return BM_ERR_INVALID;
    }

    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  ISR                                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief USART3 全局中断：错误、IDLE、TX 完成。
 */
void USART3_IRQHandler(void) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    uint32_t isr;

    if (ctx->cfg == NULL || !ctx->initialized) {
        return;
    }

    isr = USART3->ISR;
    bm_usart3_update_errors(ctx);

    if ((isr & USART_ISR_IDLE) != 0u) {
        LL_USART_ClearFlag_IDLE(USART3);
        bm_usart3_rx_update(ctx, BM_UART_EVT_IDLE, 1, 0u);
    }

    if ((isr & USART_ISR_TC) != 0u) {
        LL_USART_ClearFlag_TC(USART3);
        LL_USART_DisableIT_TC(USART3);
        ctx->tx_busy = 0;
        if (ctx->tx_complete_cb != NULL) {
            ctx->tx_complete_cb(ctx->dev, ctx->tx_complete_user);
        }
    }
}

/**
 * @brief 通用 TX DMA 中断处理。
 */
static void bm_usart3_tx_dma_isr(bm_usart3_context_t *ctx) {
    DMA_TypeDef *dma = bm_usart3_dma_ctrl(ctx->cfg->tx_dma_ctrl);
    uint32_t ch = ctx->cfg->tx_dma_ch;
    /* DMA_ISR 布局：GIF(+0)/TCIF(+1)/HTIF(+2)/TEIF(+3)，每通道 4bit
     * （stm32g474xx.h:3219-3228，对照 SPI 后端 BM_VENDOR_DMA_TC_FLAG） */
    uint32_t tc_flag = (2u << ((ch - 1u) * 4u));
    uint32_t te_flag = (1u << ((ch - 1u) * 4u + 3u));

    if ((dma->ISR & te_flag) != 0u) {
        /* 传输错误：停止 DMA，结束 TX 会话并通知上层（RS485 等须立即退出发送） */
        dma->IFCR = te_flag;
        LL_DMA_DisableChannel(dma, ch);
        LL_USART_DisableDMAReq_TX(USART3);
        LL_USART_DisableIT_TC(USART3);
        LL_USART_ClearFlag_TC(USART3);
        ctx->tx_busy = 0;
        ctx->stats.last_errors |= BM_UART_ERR_OVERRUN;
        if (ctx->tx_complete_cb != NULL && ctx->dev != NULL) {
            ctx->tx_complete_cb(ctx->dev, ctx->tx_complete_user);
        }
        return;
    }

    if ((dma->ISR & tc_flag) != 0u) {
        dma->IFCR = tc_flag;
        LL_DMA_DisableChannel(dma, ch);
        LL_USART_DisableDMAReq_TX(USART3);
        /* DMA 完成并不表示 UART TC；开启 USART TC 中断等待真正发送完成 */
        LL_USART_ClearFlag_TC(USART3);
        LL_USART_EnableIT_TC(USART3);
    }
}

/**
 * @brief 通用 RX DMA 中断处理（HT/TC）。
 */
static void bm_usart3_rx_dma_isr(bm_usart3_context_t *ctx) {
    DMA_TypeDef *dma = bm_usart3_dma_ctrl(ctx->cfg->rx_dma_ctrl);
    uint32_t ch = ctx->cfg->rx_dma_ch;
    uint32_t tc_flag = (2u << ((ch - 1u) * 4u));
    uint32_t ht_flag = (1u << ((ch - 1u) * 4u + 2u));
    uint32_t te_flag = (1u << ((ch - 1u) * 4u + 3u));

    if ((dma->ISR & te_flag) != 0u) {
        /* DMA 传输错误（总线错误）：上报并清标志，数据视为不可靠 */
        dma->IFCR = te_flag;
        ctx->stats.last_errors |= BM_UART_ERR_OVERRUN;
    }
    if ((dma->ISR & ht_flag) != 0u) {
        dma->IFCR = ht_flag;
        /* HT 仅推进写指针/溢出检测，不交付帧（避免长帧在半缓冲处被误拆） */
        bm_usart3_rx_update(ctx, 0u, 0, 0u);
    }
    if ((dma->ISR & tc_flag) != 0u) {
        uint32_t produced_before;
        uint32_t extra;

        dma->IFCR = tc_flag;
        /* TC 表示完整一圈已完成；先记录 produced_before，再推进 produced
         * 到下一周期边界，使 HT→TC 的后一半字节也被计入本次新增量，
         * 避免漏统计与溢出判断延后。
         * mark_full_round 是 produced 读-改-写，须与 IDLE ISR / recv 的
         * rx_update 互斥（全局关中断，O(1) 窗口）；extra 算出后 rx_update
         * 在锁外调用，避免把 rx_frame_cb 回调圈进 IRQ-off。 */
        {
            bm_irq_state_t irq_state = bm_critical_enter();

            produced_before = ctx->rx_ring.produced;
            bm_dma_circ_ring_mark_full_round(&ctx->rx_ring);
            extra = ctx->rx_ring.produced - produced_before;
            bm_critical_exit(irq_state);
        }
        bm_usart3_rx_update(ctx, BM_UART_EVT_RX_FULL, 1, extra);
    }
}

/**
 * @brief 按通道号调用对应 DMA handler。
 */
static void bm_usart3_dma_dispatch(bm_usart3_context_t *ctx,
                                   uint32_t ctrl, uint32_t ch) {
    if (ctx->cfg == NULL || !ctx->initialized) {
        return;
    }

    if (ctrl == ctx->cfg->tx_dma_ctrl && ch == ctx->cfg->tx_dma_ch) {
        bm_usart3_tx_dma_isr(ctx);
    }
    if (ctrl == ctx->cfg->rx_dma_ctrl && ch == ctx->cfg->rx_dma_ch) {
        bm_usart3_rx_dma_isr(ctx);
    }
}

/**
 * @brief TX DMA 路由入口（由 bm_dma_irq_stm32g4 调用）。
 */
static void bm_usart3_dma_tx_entry(void *user) {
    bm_usart3_context_t *ctx = (bm_usart3_context_t *)user;

    if (ctx == NULL || ctx->cfg == NULL) {
        return;
    }
    bm_usart3_dma_dispatch(ctx, ctx->cfg->tx_dma_ctrl, ctx->cfg->tx_dma_ch);
}

/**
 * @brief RX DMA 路由入口（由 bm_dma_irq_stm32g4 调用）。
 */
static void bm_usart3_dma_rx_entry(void *user) {
    bm_usart3_context_t *ctx = (bm_usart3_context_t *)user;

    if (ctx == NULL || ctx->cfg == NULL) {
        return;
    }
    bm_usart3_dma_dispatch(ctx, ctx->cfg->rx_dma_ctrl, ctx->cfg->rx_dma_ch);
}

/* -------------------------------------------------------------------------- */
/*  HAL API 实现                                                               */
/* -------------------------------------------------------------------------- */

static int bm_vendor_usart3_init(const struct bm_hal_uart *dev, void *config) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    const bm_usart3_stm32g4_config_t *runtime_cfg =
        (const bm_usart3_stm32g4_config_t *)config;
    const bm_usart3_stm32g4_config_t *cfg;
    GPIO_TypeDef *tx_port, *rx_port;
    DMA_TypeDef *tx_dma, *rx_dma;
    uint32_t parity_ll;
    uint32_t stop_ll;
    uint32_t data_bits;
    uint32_t actual_baud;

    cfg = (runtime_cfg != NULL) ? runtime_cfg : &g_usart3_cfg_default;

    if (bm_usart3_validate_config(cfg) != BM_OK) {
        return BM_ERR_INVALID;
    }

    tx_port = bm_usart3_port(cfg->tx_port);
    rx_port = bm_usart3_port(cfg->rx_port);
    tx_dma = bm_usart3_dma_ctrl(cfg->tx_dma_ctrl);
    rx_dma = bm_usart3_dma_ctrl(cfg->rx_dma_ctrl);
    if (tx_port == NULL || rx_port == NULL || tx_dma == NULL || rx_dma == NULL) {
        return BM_ERR_INVALID;
    }

    /* 先清状态，再初始化；失败时回滚 */
    ctx->dev = dev;
    ctx->cfg = cfg;
    ctx->initialized = 0;
    ctx->tx_busy = 0;
    ctx->rx_buf = NULL;
    ctx->rx_len = 0u;
    bm_dma_circ_ring_reset(&ctx->rx_ring, NULL, 0u);
    ctx->tx_complete_cb = NULL;
    ctx->tx_complete_user = NULL;
    ctx->rx_frame_cb = NULL;
    ctx->rx_frame_user = NULL;
    (void)memset(&ctx->stats, 0, sizeof(ctx->stats));

    /* 使能时钟 */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART3);
    bm_usart3_dma_clock_enable(cfg->tx_dma_ctrl);
    if (cfg->rx_dma_ctrl != cfg->tx_dma_ctrl) {
        bm_usart3_dma_clock_enable(cfg->rx_dma_ctrl);
    }
    bm_usart3_gpio_clock_enable(cfg->tx_port);
    if (cfg->rx_port != cfg->tx_port) {
        bm_usart3_gpio_clock_enable(cfg->rx_port);
    }

    /* 配置 GPIO */
    bm_usart3_gpio_af(tx_port, cfg->tx_pin, cfg->gpio_af);
    bm_usart3_gpio_af(rx_port, cfg->rx_pin, cfg->gpio_af);

    /* DMAMUX 请求映射 */
    LL_DMA_SetPeriphRequest(tx_dma, cfg->tx_dma_ch, cfg->tx_dma_req);
    LL_DMA_SetPeriphRequest(rx_dma, cfg->rx_dma_ch, cfg->rx_dma_req);

    LL_USART_Disable(USART3);

    switch (cfg->parity) {
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

    stop_ll = (cfg->stop_bits == BM_UART_STOPBITS_2)
                  ? LL_USART_STOPBITS_2
                  : LL_USART_STOPBITS_1;

    data_bits = (cfg->data_bits == BM_UART_DATABITS_9) ? 9u : 8u;

    {
        uint32_t ker_hz = bm_usart3_kernel_hz(cfg);

        LL_USART_SetBaudRate(USART3, ker_hz,
                             LL_USART_PRESCALER_DIV1,
                             LL_USART_OVERSAMPLING_16,
                             cfg->baud);
        actual_baud = LL_USART_GetBaudRate(USART3, ker_hz,
                                           LL_USART_PRESCALER_DIV1,
                                           LL_USART_OVERSAMPLING_16);
    }
    if (actual_baud == 0u) {
        bm_usart3_hw_deinit(ctx);
        ctx->cfg = NULL;
        return BM_ERR_INVALID;
    }

    LL_USART_ConfigAsyncMode(USART3);
    LL_USART_SetDataWidth(USART3,
        (data_bits == 9u) ? LL_USART_DATAWIDTH_9B : LL_USART_DATAWIDTH_8B);
    LL_USART_SetParity(USART3, parity_ll);
    LL_USART_SetStopBitsLength(USART3, stop_ll);
    LL_USART_SetTransferDirection(USART3, LL_USART_DIRECTION_TX_RX);

    LL_USART_EnableIT_IDLE(USART3);
    LL_USART_ClearFlag_IDLE(USART3);

    /* DMA 向量由统一路由器持有；失败则回滚，避免与 SPI 等争用同一通道 */
    if (bm_dma_irq_register((uint8_t)cfg->tx_dma_ctrl, (uint8_t)cfg->tx_dma_ch,
                            bm_usart3_dma_tx_entry, ctx) != BM_OK) {
        bm_usart3_hw_deinit(ctx);
        ctx->cfg = NULL;
        return BM_ERR_BUSY;
    }
    if (cfg->rx_dma_ctrl != cfg->tx_dma_ctrl ||
        cfg->rx_dma_ch != cfg->tx_dma_ch) {
        if (bm_dma_irq_register((uint8_t)cfg->rx_dma_ctrl,
                                (uint8_t)cfg->rx_dma_ch,
                                bm_usart3_dma_rx_entry, ctx) != BM_OK) {
            bm_usart3_hw_deinit(ctx);
            ctx->cfg = NULL;
            return BM_ERR_BUSY;
        }
    }

    NVIC_SetPriority(cfg->usart_irqn, cfg->irq_priority);
    NVIC_EnableIRQ(cfg->usart_irqn);
    NVIC_SetPriority(cfg->tx_dma_irqn, cfg->tx_dma_irq_priority);
    NVIC_EnableIRQ(cfg->tx_dma_irqn);
    NVIC_SetPriority(cfg->rx_dma_irqn, cfg->rx_dma_irq_priority);
    NVIC_EnableIRQ(cfg->rx_dma_irqn);

    LL_USART_Enable(USART3);

    ctx->initialized = 1;
    return BM_OK;
}

/**
 * @brief 通过USART发送数据。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param data 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID；资源忙或队列已满时返回 BM_ERR_BUSY。
 */
static int bm_vendor_usart3_send(const struct bm_hal_uart *dev,
                                 const uint8_t *data, size_t len) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    DMA_TypeDef *dma;
    uint32_t ch;

    (void)dev;
    if (ctx->initialized == 0 || data == NULL || len == 0u) {
        return BM_ERR_INVALID;
    }
    if (ctx->tx_busy != 0) {
        return BM_ERR_BUSY;
    }

    dma = bm_usart3_dma_ctrl(ctx->cfg->tx_dma_ctrl);
    ch = ctx->cfg->tx_dma_ch;

    ctx->tx_busy = 1;
    ctx->stats.tx_count += (uint32_t)len;

    bm_usart3_tx_dma_config(ctx, dma, ch, data, len);
    LL_DMA_EnableIT_TC(dma, ch);
    LL_DMA_EnableIT_TE(dma, ch);
    LL_DMA_EnableChannel(dma, ch);

    LL_USART_ClearFlag_TC(USART3);
    LL_USART_EnableDMAReq_TX(USART3);
    return BM_OK;
}

/**
 * @brief 从USART接收数据。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param data 接收数据缓冲区。
 * @param max_len 接收缓冲区容量，单位为字节。
 * @return 实际写入接收缓冲区的字节数；无数据或参数无效时返回 0。
 */
static size_t bm_vendor_usart3_recv(const struct bm_hal_uart *dev,
                                    uint8_t *data, size_t max_len) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    uint32_t copied;

    (void)dev;
    if (ctx->initialized == 0 || data == NULL || max_len == 0u ||
        ctx->rx_buf == NULL || ctx->rx_len == 0u) {
        return 0u;
    }

    /* 同步一次写位置，确保读取的是最新数据。update + consume 须整体互斥：
     * consume 逐字节 RMW consumed，若中途被 ISR 的 drop_all（consumed =
     * produced）抢占，consumed 会越过 produced 导致 pending 下溢回绕。
     * rx_update(notify=0) 内部不会再触回调，故整段可安全置于 IRQ-off；
     * 窗口上界 O(max_len)，有界（与事件队列 inline memcpy 的 IRQ-off
     * 先例一致）。 */
    {
        bm_irq_state_t irq_state = bm_critical_enter();

        bm_usart3_rx_update(ctx, 0u, 0, 0u);
        copied = bm_dma_circ_ring_consume(&ctx->rx_ring, data,
                                          (uint32_t)max_len);
        bm_critical_exit(irq_state);
    }
    return (size_t)copied;
}

/**
 * @brief 设置 USART 单字节接收回调。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param cb 单字节接收回调；当前驱动不支持该回调并忽略此参数，上层应使用接收帧回调。
 */
static void bm_vendor_usart3_set_rx_callback(const struct bm_hal_uart *dev,
                                             void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
    /* 本后端不支持单字节回调；上层应使用 set_rx_frame_callback */
}

/**
 * @brief 中止USART当前传输。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @return 成功返回 BM_OK；设备未初始化时返回 BM_ERR_NOT_INIT。
 */
static int bm_vendor_usart3_abort(const struct bm_hal_uart *dev) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }

    bm_usart3_tx_dma_stop(ctx);
    LL_USART_DisableIT_TC(USART3);
    ctx->tx_busy = 0;

    bm_usart3_rx_dma_stop(ctx);
    bm_dma_circ_ring_reset(&ctx->rx_ring, ctx->rx_buf,
                           (uint32_t)ctx->rx_len);
    return BM_OK;
}

/**
 * @brief 等待USART发送数据完成。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @return 成功返回 BM_OK；设备未初始化时返回 BM_ERR_NOT_INIT；等待超时时返回 BM_ERR_TIMEOUT。
 */
static int bm_vendor_usart3_flush(const struct bm_hal_uart *dev) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;
    uint32_t timeout = 100000u;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }

    /* 等待 tx_busy 释放（USART TC 中断中清零） */
    while (ctx->tx_busy != 0 && timeout != 0u) {
        timeout--;
    }
    return (ctx->tx_busy == 0) ? BM_OK : BM_ERR_TIMEOUT;
}

/**
 * @brief 设置USART发送完成回调。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param cb 发送完成回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备未初始化时返回 BM_ERR_NOT_INIT。
 */
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

/**
 * @brief 设置USART接收帧回调。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param cb 接收帧回调；传入 NULL 时解除绑定。
 * @param user 回调用户上下文，调用回调时原样传入。
 * @return 成功返回 BM_OK；设备未初始化时返回 BM_ERR_NOT_INIT。
 */
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

/**
 * @brief 设置USART接收缓冲区。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param buf 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK；设备未初始化时返回 BM_ERR_NOT_INIT；设备或参数无效时返回 BM_ERR_INVALID。
 */
static int bm_vendor_usart3_set_rx_buffer(const struct bm_hal_uart *dev,
                                          uint8_t *buf, size_t len) {
    bm_usart3_context_t *ctx = &g_usart3_ctx;

    (void)dev;
    if (ctx->initialized == 0) {
        return BM_ERR_NOT_INIT;
    }

    bm_usart3_rx_dma_stop(ctx);

    if (buf == NULL || len == 0u) {
        ctx->rx_buf = NULL;
        ctx->rx_len = 0u;
        bm_dma_circ_ring_reset(&ctx->rx_ring, NULL, 0u);
        return BM_ERR_INVALID;
    }

    ctx->rx_buf = buf;
    ctx->rx_len = len;
    bm_usart3_rx_dma_start(ctx);
    return BM_OK;
}

/**
 * @brief 读取USART运行统计。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param stats 用于接收运行统计的输出结构；不得为 NULL。
 * @return 成功返回 BM_OK；设备或参数无效时返回 BM_ERR_INVALID。
 */
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

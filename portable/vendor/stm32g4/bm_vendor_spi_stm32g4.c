/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_spi_stm32g4.c
 * @brief STM32G474xB SPI1 驱动（bm_drv_spi 契约，阻塞全双工轮询，STM32 LL 库）
 * @maturity E1
 *
 * transfer 首次调用懒初始化：按设备 config（bm_spi_config_t）配置 SPI1
 * 主机模式（8bit、MSB first、软件 NSS）、GPIO 复用与 CS 引脚；cs_managed
 * 时 transfer 内自动拉低/拉高 CS（经 config 指定的 bm_hal_gpio 设备）。
 * SCK 分频按 PCLK2 就近取 ≥目标时钟的档位（LL_SPI_BAUDRATEPRESCALER_DIV2..
 * DIV256 连续编码）。
 *
 * 引脚绑定走 bm_hal_instances_stm32g4.h 宏（默认 PA5/PA6/PA7 AF5，CS PA4）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 * 2026-07-27       1.1            zeh            追加 transfer_async（DMA 双通道 + RX 全满 ISR
 *                                                完成回调，FPU 守卫包裹）；CS 操作直调 GPIO
 *                                                vtable（vendor 不依赖 bm_hal 分发）
 * 2026-07-28       1.2            zeh            DMA IRQ 改 bm_dma_irq 路由器注册
 *
 * 2026-08-01       1.2            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_vendor_spi_stm32g4.h"
#include "bm_vendor_gpio_stm32g4.h"
#include "bm_dma_irq_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_spi.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_dmamux.h"
#include "armv7em/bm_arch_isr_fpu.h"

/** @brief 硬件就绪轮询上限（TXE/RXNE/BSY 等待，路径必须有界）。 */
#define BM_VENDOR_SPI_POLL_LIMIT  100000u

/** @brief SPI DMA 完成 ISR 的 FPU 现场保存区占位（armv7em 上守卫 no-op）。 */
static uint8_t g_spi_fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));

/** @brief SPI1 默认配置（CS 经全芯片 GPIO 设备 bm_stm32g4_gpio）。 */
const bm_spi_config_t bm_stm32g4_spi1_default_config = {
    BM_STM32G4_SPI1_CLOCK_HZ,
    BM_STM32G4_SPI1_MODE,
    &bm_stm32g4_gpio,
    BM_GPIO_PIN_ENCODE(0u, BM_STM32G4_SPI1_CS_PIN),
    1u,
};

/** @brief 硬件是否已初始化（lazy init 幂等标志）。 */
static int g_spi1_initialized;

/**
 * @brief GPIO 复用配置（推挽、高速、无上下拉）。
 */
static void bm_vendor_spi_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
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
 * @brief 初始化 SPI1 硬件（幂等，由 transfer 懒调用）。
 * @param cfg 设备配置（时钟/模式/CS）。
 * @return BM_OK 成功；BM_ERR_INVALID 配置非法。
 */
static int bm_vendor_spi_hw_init(const bm_spi_config_t *cfg)
{
    LL_RCC_ClocksTypeDef clocks;
    uint32_t pclk2;
    uint32_t presc_code = 0u;
    uint32_t pol;
    uint32_t pha;

    if (g_spi1_initialized != 0) {
        return BM_OK;
    }
    if (cfg == NULL || cfg->mode > BM_SPI_MODE_3 || cfg->clock_hz == 0u) {
        return BM_ERR_INVALID;
    }

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

    bm_vendor_spi_gpio_af(GPIOA, BM_STM32G4_SPI1_SCK_PIN,  BM_STM32G4_SPI1_GPIO_AF);
    bm_vendor_spi_gpio_af(GPIOA, BM_STM32G4_SPI1_MISO_PIN, BM_STM32G4_SPI1_GPIO_AF);
    bm_vendor_spi_gpio_af(GPIOA, BM_STM32G4_SPI1_MOSI_PIN, BM_STM32G4_SPI1_GPIO_AF);

    /* CS 引脚经 GPIO 设备配置为推挽输出并置高（空闲）。
     * 直接调 vtable 而不经 bm_hal_gpio_* 分发：vendor 不依赖 bm_hal
     * （静态库单遍链接下 bm_hal 先于 vendor 扫描，经分发会未解析）。 */
    if (cfg->cs_gpio != NULL && cfg->cs_gpio->api != NULL
        && cfg->cs_managed != 0u) {
        (void)cfg->cs_gpio->api->configure(cfg->cs_gpio, cfg->cs_pin,
                                           BM_GPIO_OUTPUT);
        (void)cfg->cs_gpio->api->write(cfg->cs_gpio, cfg->cs_pin, 1);
    }

    /* 分频：取最小档使 SCK <= 目标（DIV2^code+1，code 0..7） */
    LL_RCC_GetSystemClocksFreq(&clocks);
    pclk2 = clocks.PCLK2_Frequency;
    while (presc_code < 7u && (pclk2 >> (presc_code + 1u)) > cfg->clock_hz) {
        presc_code++;
    }

    pol = (cfg->mode >= BM_SPI_MODE_2) ? LL_SPI_POLARITY_HIGH : LL_SPI_POLARITY_LOW;
    pha = ((cfg->mode & 1u) != 0u) ? LL_SPI_PHASE_2EDGE : LL_SPI_PHASE_1EDGE;

    LL_SPI_Disable(SPI1);
    LL_SPI_SetMode(SPI1, LL_SPI_MODE_MASTER);
    LL_SPI_SetClockPolarity(SPI1, pol);
    LL_SPI_SetClockPhase(SPI1, pha);
    LL_SPI_SetBaudRatePrescaler(SPI1,
        LL_SPI_BAUDRATEPRESCALER_DIV2 + (presc_code << SPI_CR1_BR_Pos));
    LL_SPI_SetTransferBitOrder(SPI1, LL_SPI_MSB_FIRST);
    LL_SPI_SetDataWidth(SPI1, LL_SPI_DATAWIDTH_8BIT);
    LL_SPI_SetNSSMode(SPI1, LL_SPI_NSS_SOFT);
    LL_SPI_Enable(SPI1);

    g_spi1_initialized = 1;
    return BM_OK;
}

/**
 * @brief 阻塞全双工传输（有界轮询 TXE/RXNE；cs_managed 自动拉 CS）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数/配置非法；BM_ERR_TIMEOUT 硬件未就绪。
 */
static int bm_vendor_spi_transfer(const struct bm_hal_spi *dev,
                                  const uint8_t *tx, uint8_t *rx, size_t len)
{
    const bm_spi_config_t *cfg;
    size_t   i;
    uint32_t wait;
    int      rc;

    if (dev == NULL || dev->config == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_spi_config_t *)dev->config;
    rc = bm_vendor_spi_hw_init(cfg);
    if (rc != BM_OK) {
        return rc;
    }

    if (cfg->cs_gpio != NULL && cfg->cs_gpio->api != NULL
        && cfg->cs_managed != 0u) {
        (void)cfg->cs_gpio->api->write(cfg->cs_gpio, cfg->cs_pin, 0);
    }

    rc = BM_OK;
    for (i = 0u; i < len; ++i) {
        for (wait = 0u; wait < BM_VENDOR_SPI_POLL_LIMIT; ++wait) {
            if (LL_SPI_IsActiveFlag_TXE(SPI1) != 0u) {
                break;
            }
        }
        if (wait >= BM_VENDOR_SPI_POLL_LIMIT) {
            rc = BM_ERR_TIMEOUT;
            break;
        }
        LL_SPI_TransmitData8(SPI1, (tx != NULL) ? tx[i] : 0xFFu);
        for (wait = 0u; wait < BM_VENDOR_SPI_POLL_LIMIT; ++wait) {
            if (LL_SPI_IsActiveFlag_RXNE(SPI1) != 0u) {
                break;
            }
        }
        if (wait >= BM_VENDOR_SPI_POLL_LIMIT) {
            rc = BM_ERR_TIMEOUT;
            break;
        }
        {
            uint8_t b = LL_SPI_ReceiveData8(SPI1);
            if (rx != NULL) {
                rx[i] = b;
            }
        }
    }

    /* 等总线空闲再释放 CS（避免截断最后字节） */
    for (wait = 0u; wait < BM_VENDOR_SPI_POLL_LIMIT; ++wait) {
        if (LL_SPI_IsActiveFlag_BSY(SPI1) == 0u) {
            break;
        }
    }
    if (cfg->cs_gpio != NULL && cfg->cs_gpio->api != NULL
        && cfg->cs_managed != 0u) {
        (void)cfg->cs_gpio->api->write(cfg->cs_gpio, cfg->cs_pin, 1);
    }
    return rc;
}

/* ---------- SPI1 异步 DMA（transfer_async，RX DMA 全满 ISR 完成） ---------- */

/** @brief RX DMA 通道 LL 索引（0-based = 通道号-1）。 */
#define BM_VENDOR_SPI_DMA_RX_CH  (BM_STM32G4_SPI1_DMA_RX_CH - 1u)
/** @brief TX DMA 通道 LL 索引。 */
#define BM_VENDOR_SPI_DMA_TX_CH  (BM_STM32G4_SPI1_DMA_TX_CH - 1u)
/**
 * @brief DMA ISR/IFCR 标志位计算（TC=4×ch+1，ch 为 0-based 通道索引）。
 * LL 只提供按通道的 LL_DMA_IsActiveFlag_TC1..8 逐个函数，无按通道号通用
 * 标志 API，此处直接操作 ISR/IFCR 寄存器（注释在案）。
 */
#define BM_VENDOR_DMA_TC_FLAG(ch)  (1u << ((ch) * 4u + 1u))

static bm_spi_transfer_done_fn_t g_spi_async_cb;
static void                     *g_spi_async_user;
static const struct bm_hal_spi  *g_spi_async_dev;
static uint8_t                   g_spi_async_busy;
/** @brief 只发场景的接收丢弃占位 / 只收场景的发送 0xFF 占位。 */
static uint8_t                   g_spi_dummy_rx;
static uint8_t                   g_spi_dummy_tx = 0xFFu;

/** @brief CS 拉低/拉高（cs_managed 时，直调 GPIO vtable，理由见 hw_init）。 */
static void bm_vendor_spi_cs_write(const bm_spi_config_t *cfg, int level)
{
    if (cfg->cs_gpio != NULL && cfg->cs_gpio->api != NULL
        && cfg->cs_managed != 0u) {
        (void)cfg->cs_gpio->api->write(cfg->cs_gpio, cfg->cs_pin, level);
    }
}

/**
 * @brief SPI1 RX DMA 路由入口（通道由 instances 宏决定）。
 */
static void bm_vendor_spi_dma_irq_entry(void *user);

/**
 * @brief 异步全双工传输（DMA 双通道；RX 全满 ISR 中收尾并触发 done_cb）。
 *
 * 语义（契约）：启动即返回；done_cb 于 ISR 上下文触发（FPU 守卫包裹），
 * 回调时 tx/rx 缓冲区所有权归还调用方。
 *
 * @return BM_OK 已启动；BM_ERR_INVALID 参数/配置非法；BM_ERR_BUSY 上一笔未完成
 *         或 DMA 通道已被其它驱动占用。
 */
static int bm_vendor_spi_transfer_async(const struct bm_hal_spi *dev,
                                        const uint8_t *tx, uint8_t *rx,
                                        size_t len,
                                        bm_spi_transfer_done_fn_t done_cb,
                                        void *user)
{
    const bm_spi_config_t *cfg;
    int rc;

    if (dev == NULL || dev->config == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_spi_config_t *)dev->config;
    rc = bm_vendor_spi_hw_init(cfg);
    if (rc != BM_OK) {
        return rc;
    }
    if (g_spi_async_busy != 0u) {
        return BM_ERR_BUSY;
    }

    rc = bm_dma_irq_register(1u, (uint8_t)BM_STM32G4_SPI1_DMA_RX_CH,
                             bm_vendor_spi_dma_irq_entry, NULL);
    if (rc != BM_OK) {
        return rc;
    }

    g_spi_async_busy = 1u;
    g_spi_async_cb   = done_cb;
    g_spi_async_user = user;
    g_spi_async_dev  = dev;

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1
                             | LL_AHB1_GRP1_PERIPH_DMAMUX1);

    LL_DMA_DisableChannel(DMA1, BM_VENDOR_SPI_DMA_RX_CH);
    LL_DMA_DisableChannel(DMA1, BM_VENDOR_SPI_DMA_TX_CH);
    LL_DMAMUX_SetRequestID(DMAMUX1, BM_VENDOR_SPI_DMA_RX_CH,
                           BM_STM32G4_SPI1_DMA_RX_REQ);
    LL_DMAMUX_SetRequestID(DMAMUX1, BM_VENDOR_SPI_DMA_TX_CH,
                           BM_STM32G4_SPI1_DMA_TX_REQ);

    /* RX 通道：外设→内存 */
    LL_DMA_SetPeriphAddress(DMA1, BM_VENDOR_SPI_DMA_RX_CH,
                            (uint32_t)&SPI1->DR);
    LL_DMA_SetMemoryAddress(DMA1, BM_VENDOR_SPI_DMA_RX_CH,
                            (rx != NULL) ? (uint32_t)rx
                                         : (uint32_t)&g_spi_dummy_rx);
    LL_DMA_SetDataLength(DMA1, BM_VENDOR_SPI_DMA_RX_CH, (uint32_t)len);
    LL_DMA_ConfigTransfer(DMA1, BM_VENDOR_SPI_DMA_RX_CH,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_NORMAL
        | LL_DMA_PERIPH_NOINCREMENT
        | ((rx != NULL) ? LL_DMA_MEMORY_INCREMENT : LL_DMA_MEMORY_NOINCREMENT)
        | LL_DMA_PDATAALIGN_BYTE | LL_DMA_MDATAALIGN_BYTE
        | LL_DMA_PRIORITY_HIGH);

    /* TX 通道：内存→外设 */
    LL_DMA_SetPeriphAddress(DMA1, BM_VENDOR_SPI_DMA_TX_CH,
                            (uint32_t)&SPI1->DR);
    LL_DMA_SetMemoryAddress(DMA1, BM_VENDOR_SPI_DMA_TX_CH,
                            (tx != NULL) ? (uint32_t)tx
                                         : (uint32_t)&g_spi_dummy_tx);
    LL_DMA_SetDataLength(DMA1, BM_VENDOR_SPI_DMA_TX_CH, (uint32_t)len);
    LL_DMA_ConfigTransfer(DMA1, BM_VENDOR_SPI_DMA_TX_CH,
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_MODE_NORMAL
        | LL_DMA_PERIPH_NOINCREMENT
        | ((tx != NULL) ? LL_DMA_MEMORY_INCREMENT : LL_DMA_MEMORY_NOINCREMENT)
        | LL_DMA_PDATAALIGN_BYTE | LL_DMA_MDATAALIGN_BYTE
        | LL_DMA_PRIORITY_HIGH);

    /* 清双通道全部挂起标志（LL 无按通道号通用清标志 API，写 IFCR） */
    DMA1->IFCR = 0xFu << (BM_VENDOR_SPI_DMA_RX_CH * 4u)
                 | 0xFu << (BM_VENDOR_SPI_DMA_TX_CH * 4u);
    LL_DMA_EnableIT_TC(DMA1, BM_VENDOR_SPI_DMA_RX_CH);
    NVIC_SetPriority((IRQn_Type)(DMA1_Channel1_IRQn
                                 + (int)BM_VENDOR_SPI_DMA_RX_CH),
                     BM_STM32G4_SPI1_DMA_IRQ_PRIORITY);
    NVIC_EnableIRQ((IRQn_Type)(DMA1_Channel1_IRQn
                               + (int)BM_VENDOR_SPI_DMA_RX_CH));

    bm_vendor_spi_cs_write(cfg, 0);
    LL_DMA_EnableChannel(DMA1, BM_VENDOR_SPI_DMA_RX_CH);
    LL_DMA_EnableChannel(DMA1, BM_VENDOR_SPI_DMA_TX_CH);
    LL_SPI_EnableDMAReq_RX(SPI1);
    LL_SPI_EnableDMAReq_TX(SPI1);
    return BM_OK;
}

/**
 * @brief SPI1 RX DMA 全满 ISR 公共体。
 */
static void bm_vendor_spi_dma_rx_isr(void)
{
    const bm_spi_config_t *cfg =
        (const bm_spi_config_t *)g_spi_async_dev->config;
    unsigned fpu_prev;

    LL_DMA_DisableChannel(DMA1, BM_VENDOR_SPI_DMA_RX_CH);
    LL_DMA_DisableChannel(DMA1, BM_VENDOR_SPI_DMA_TX_CH);
    LL_SPI_DisableDMAReq_RX(SPI1);
    LL_SPI_DisableDMAReq_TX(SPI1);
    bm_vendor_spi_cs_write(cfg, 1);
    g_spi_async_busy = 0u;

    fpu_prev = bm_arch_isr_fpu_enter(g_spi_fpu_sa);
    if (g_spi_async_cb != NULL) {
        g_spi_async_cb(g_spi_async_dev, g_spi_async_user);
    }
    bm_arch_isr_fpu_exit(g_spi_fpu_sa, fpu_prev);
}

/**
 * @brief SPI1 RX DMA 路由入口（通道由 instances 宏决定）。
 */
static void bm_vendor_spi_dma_irq_entry(void *user)
{
    (void)user;
    if ((DMA1->ISR & BM_VENDOR_DMA_TC_FLAG(BM_VENDOR_SPI_DMA_RX_CH)) != 0u) {
        DMA1->IFCR = BM_VENDOR_DMA_TC_FLAG(BM_VENDOR_SPI_DMA_RX_CH);
        bm_vendor_spi_dma_rx_isr();
    }
}

/** @brief SPI 驱动 API 表（transfer_async 为本后端提供的可选异步能力）。 */
static const struct bm_spi_driver_api g_spi_api = {
    bm_vendor_spi_transfer,
    bm_vendor_spi_transfer_async,
};

/** @brief STM32G4 SPI1 设备。 */
const bm_hal_spi_t bm_stm32g4_spi1 = { &g_spi_api, &bm_stm32g4_spi1_default_config };

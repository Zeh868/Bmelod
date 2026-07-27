/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_uart_dev_stm32g4.c
 * @brief STM32G474xB USART2 设备实例驱动（bm_drv_uart 实例契约，STM32 LL 库）
 *
 * 与 LPUART1 单例（console）并存：多 UART 场景（TMC2209/RS485）走本实例。
 * 支持 HDSEL 单线半双工（仅 TX 脚，TMC 单线拓扑；此时 RX 侧数据来自同一
 * 线上对端，发送字节的回环处理由上层协议承担，见 tmc2209 组件）。
 *
 * 引脚/波特率/单线标志走 bm_hal_instances_stm32g4.h 宏（默认 PA9/PA10 AF7，
 * 115200，单线使能）；init 的 config 入参可运行时覆盖波特率与单线标志。
 *
 * 保留 CMSIS 写法的位置：NVIC 优先级/使能（LL 无 NVIC 抽象，用 CMSIS core
 * 的 NVIC_* 函数，逐处注释）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 *
 */
#include "bm_vendor_uart_dev_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_usart.h"

/* ---------- 全局状态 ---------- */
static void (*g_rx_callback)(uint8_t c);
static uint8_t g_uart_ready;

/**
 * @brief GPIO 复用配置（推挽、高速、无上下拉）。
 */
static void bm_vendor_uart_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
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
 * @brief 初始化 USART2 设备实例（幂等）。
 *
 * @param dev    UART 设备实例（未使用，单实例）。
 * @param config 运行时配置；NULL 或字段为 0 时取 instances 宏默认值。
 * @return BM_OK。
 */
static int bm_vendor_uart_dev_init(const struct bm_hal_uart *dev,
                                   void *config)
{
    const bm_stm32g4_uart_dev_config_t *cfg =
        (const bm_stm32g4_uart_dev_config_t *)config;
    LL_RCC_ClocksTypeDef clocks;
    uint32_t baud        = BM_STM32G4_USART2_BAUD;
    uint8_t  single_wire = BM_STM32G4_USART2_SINGLE_WIRE;

    (void)dev;
    if (cfg != NULL) {
        if (cfg->baud != 0u) {
            baud = cfg->baud;
        }
        if (cfg->single_wire != 0u) {
            single_wire = cfg->single_wire;
        }
    }

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);

    bm_vendor_uart_gpio_af(GPIOA, BM_STM32G4_USART2_TX_PIN,
                           BM_STM32G4_USART2_GPIO_AF);
    if (single_wire == 0u) {
        bm_vendor_uart_gpio_af(GPIOA, BM_STM32G4_USART2_RX_PIN,
                               BM_STM32G4_USART2_GPIO_AF);
    }

    LL_USART_Disable(USART2);
    if (single_wire != 0u) {
        LL_USART_EnableHalfDuplex(USART2);
    } else {
        LL_USART_DisableHalfDuplex(USART2);
    }
    LL_RCC_GetSystemClocksFreq(&clocks);
    LL_USART_SetBaudRate(USART2, clocks.PCLK1_Frequency,
                         LL_USART_PRESCALER_DIV1, LL_USART_OVERSAMPLING_16,
                         baud);
    LL_USART_EnableDirectionTx(USART2);
    LL_USART_EnableDirectionRx(USART2);
    LL_USART_Enable(USART2);

    g_uart_ready = 1u;
    return BM_OK;
}

/**
 * @brief 发送字节流（轮询 TXE，有界性同 console 单例语义）。
 */
static int bm_vendor_uart_dev_send(const struct bm_hal_uart *dev,
                                   const uint8_t *data, size_t len)
{
    size_t i;

    (void)dev;
    if (data == NULL) {
        return BM_ERR_INVALID;
    }
    if (g_uart_ready == 0u) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < len; ++i) {
        while (LL_USART_IsActiveFlag_TXE(USART2) == 0u) {
        }
        LL_USART_TransmitData8(USART2, data[i]);
    }
    return BM_OK;
}

/**
 * @brief 非阻塞轮询接收：RXNE 有数据则读出，无数据立即返回 0。
 */
static size_t bm_vendor_uart_dev_recv(const struct bm_hal_uart *dev,
                                      uint8_t *data, size_t max_len)
{
    size_t n = 0u;

    (void)dev;
    if (data == NULL || max_len == 0u) {
        return 0u;
    }
    if (g_uart_ready == 0u) {
        return 0u;
    }
    while (n < max_len && LL_USART_IsActiveFlag_RXNE(USART2) != 0u) {
        data[n++] = LL_USART_ReceiveData8(USART2);
    }
    return n;
}

/**
 * @brief USART2 RX 中断：读 RDR 清 RXNE，逐字节派发注册的 RX 回调。
 */
void USART2_IRQHandler(void)
{
    while (LL_USART_IsActiveFlag_RXNE(USART2) != 0u) {
        uint8_t c = LL_USART_ReceiveData8(USART2);
        if (g_rx_callback != NULL) {
            g_rx_callback(c);
        }
    }
}

/**
 * @brief 注册 RX 回调；设置时打开 RXNE 中断源，NULL 时先关中断源再清回调。
 */
static void bm_vendor_uart_dev_set_rx_callback(const struct bm_hal_uart *dev,
                                               void (*cb)(uint8_t c))
{
    (void)dev;
    if (cb == NULL) {
        LL_USART_DisableIT_RXNE(USART2);
        NVIC_DisableIRQ(USART2_IRQn); /* NVIC 无 LL API，用 CMSIS core 函数 */
        g_rx_callback = NULL;
        return;
    }
    g_rx_callback = cb;
    LL_USART_EnableIT_RXNE(USART2);
    NVIC_SetPriority(USART2_IRQn, BM_STM32G4_USART2_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART2_IRQn);
}

/** @brief USART2 设备实例 API 表。 */
static const struct bm_uart_driver_api g_uart_dev_api = {
    bm_vendor_uart_dev_init,
    bm_vendor_uart_dev_send,
    bm_vendor_uart_dev_recv,
    bm_vendor_uart_dev_set_rx_callback,
};

/** @brief STM32G4 USART2 设备实例。 */
const bm_hal_uart_t bm_stm32g4_uart_dev_usart2 = { &g_uart_dev_api, NULL };

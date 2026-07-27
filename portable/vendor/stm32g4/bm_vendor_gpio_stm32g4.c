/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_gpio_stm32g4.c
 * @brief STM32G474xB GPIO 驱动（bm_drv_gpio 契约，STM32 LL 库）
 *
 * 整个芯片一个设备 bm_stm32g4_gpio，pin 编码 (port<<4)|num
 * （0=A .. 6=G）。configure 时按端口懒使能 AHB2 时钟；
 * flags 语义见 include/drv/bm_drv_gpio.h（不做中断绑定与 AF 配置，
 * AF 属各外设 vendor 文件内部）。
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
#include "bm_vendor_gpio_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"

/** @brief 端口数（GPIOA..GPIOG）。 */
#define BM_VENDOR_GPIO_PORT_COUNT  7u

/** @brief 端口寄存器基址表（index = pin 编码高 4 位）。 */
static GPIO_TypeDef *const s_gpio_ports[BM_VENDOR_GPIO_PORT_COUNT] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG,
};

/** @brief 端口 AHB2 时钟使能位表（GPIOAEN..GPIOGEN 在 AHB2ENR 连续）。 */
static const uint32_t s_gpio_clk[BM_VENDOR_GPIO_PORT_COUNT] = {
    LL_AHB2_GRP1_PERIPH_GPIOA, LL_AHB2_GRP1_PERIPH_GPIOB,
    LL_AHB2_GRP1_PERIPH_GPIOC, LL_AHB2_GRP1_PERIPH_GPIOD,
    LL_AHB2_GRP1_PERIPH_GPIOE, LL_AHB2_GRP1_PERIPH_GPIOF,
    LL_AHB2_GRP1_PERIPH_GPIOG,
};

/**
 * @brief 解码 pin 并校验端口合法性。
 * @param pin  pin 编码。
 * @param port 输出端口寄存器指针。
 * @param mask 输出引脚掩码。
 * @return BM_OK 合法；BM_ERR_INVALID 端口越界。
 */
static int bm_vendor_gpio_decode(uint32_t pin,
                                 GPIO_TypeDef **port, uint32_t *mask)
{
    uint32_t p = BM_GPIO_PIN_PORT(pin);

    if (p >= BM_VENDOR_GPIO_PORT_COUNT) {
        return BM_ERR_INVALID;
    }
    *port = s_gpio_ports[p];
    *mask = 1u << BM_GPIO_PIN_NUM(pin);
    return BM_OK;
}

/**
 * @brief 配置引脚（方向/开漏/上下拉/模拟），端口时钟懒使能。
 */
static int bm_vendor_gpio_configure(const struct bm_hal_gpio *dev,
                                    uint32_t pin, uint32_t flags)
{
    GPIO_TypeDef *port;
    uint32_t      mask;
    uint32_t      mode;
    uint32_t      pull;

    (void)dev;
    if (bm_vendor_gpio_decode(pin, &port, &mask) != BM_OK) {
        return BM_ERR_INVALID;
    }
    LL_AHB2_GRP1_EnableClock(s_gpio_clk[BM_GPIO_PIN_PORT(pin)]);

    if ((flags & BM_GPIO_ANALOG) != 0u) {
        mode = LL_GPIO_MODE_ANALOG;
    } else if ((flags & BM_GPIO_OUTPUT) != 0u) {
        mode = LL_GPIO_MODE_OUTPUT;
    } else {
        mode = LL_GPIO_MODE_INPUT;
    }
    if ((flags & BM_GPIO_PULL_UP) != 0u) {
        pull = LL_GPIO_PULL_UP;
    } else if ((flags & BM_GPIO_PULL_DOWN) != 0u) {
        pull = LL_GPIO_PULL_DOWN;
    } else {
        pull = LL_GPIO_PULL_NO;
    }

    LL_GPIO_SetPinMode(port, mask, mode);
    LL_GPIO_SetPinPull(port, mask, pull);
    if (mode == LL_GPIO_MODE_OUTPUT) {
        LL_GPIO_SetPinOutputType(port, mask,
            ((flags & BM_GPIO_OUTPUT_OD) == BM_GPIO_OUTPUT_OD)
                ? LL_GPIO_OUTPUT_OPENDRAIN : LL_GPIO_OUTPUT_PUSHPULL);
        LL_GPIO_SetPinSpeed(port, mask, LL_GPIO_SPEED_FREQ_HIGH);
    }
    return BM_OK;
}

/**
 * @brief 写引脚电平。
 */
static int bm_vendor_gpio_write(const struct bm_hal_gpio *dev,
                                uint32_t pin, int value)
{
    GPIO_TypeDef *port;
    uint32_t      mask;

    (void)dev;
    if (bm_vendor_gpio_decode(pin, &port, &mask) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (value != 0) {
        LL_GPIO_SetOutputPin(port, mask);
    } else {
        LL_GPIO_ResetOutputPin(port, mask);
    }
    return BM_OK;
}

/**
 * @brief 读引脚电平（读输入数据寄存器，反映引脚真实电平）。
 */
static int bm_vendor_gpio_read(const struct bm_hal_gpio *dev,
                               uint32_t pin, int *value)
{
    GPIO_TypeDef *port;
    uint32_t      mask;

    (void)dev;
    if (value == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_vendor_gpio_decode(pin, &port, &mask) != BM_OK) {
        return BM_ERR_INVALID;
    }
    *value = (LL_GPIO_IsInputPinSet(port, mask) != 0u) ? 1 : 0;
    return BM_OK;
}

/**
 * @brief 翻转引脚电平。
 */
static int bm_vendor_gpio_toggle(const struct bm_hal_gpio *dev, uint32_t pin)
{
    GPIO_TypeDef *port;
    uint32_t      mask;

    (void)dev;
    if (bm_vendor_gpio_decode(pin, &port, &mask) != BM_OK) {
        return BM_ERR_INVALID;
    }
    LL_GPIO_TogglePin(port, mask);
    return BM_OK;
}

/** @brief GPIO 驱动 API 表。 */
static const struct bm_gpio_driver_api g_gpio_api = {
    bm_vendor_gpio_configure,
    bm_vendor_gpio_write,
    bm_vendor_gpio_read,
    bm_vendor_gpio_toggle,
};

/** @brief STM32G4 全芯片 GPIO 设备。 */
const bm_hal_gpio_t bm_stm32g4_gpio = { &g_gpio_api, NULL };

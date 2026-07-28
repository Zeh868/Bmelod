/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_gpio_stm32g4.c
 * @brief STM32G474xB GPIO 驱动（bm_drv_gpio 契约，STM32 LL 库，含 EXTI）
 *
 * 整个芯片一个设备 bm_stm32g4_gpio，pin 编码 (port<<4)|num
 * （0=A .. 6=G）。configure 时按端口懒使能 AHB2 时钟；
 * flags 语义见 include/drv/bm_drv_gpio.h（不做 AF 配置，AF 属各外设
 * vendor 文件内部）。
 *
 * EXTI：SYSCFG 端口映射 + RTSR/FTSR/IMR，NVIC 向量 EXTI0..4、
 * EXTI9_5、EXTI15_10；同一 EXTI line 不可绑定不同端口。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1）
 * 2026-07-28       1.1            zeh            增加 EXTI 配置/使能/pending 清除桩
 * 2026-07-28       1.2            zeh            实现 EXTI 真后端与 IRQ Handler
 *
 */
#include "bm_vendor_gpio_stm32g4.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_exti.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_system.h"

/** @brief 端口数（GPIOA..GPIOG）。 */
#define BM_VENDOR_GPIO_PORT_COUNT  7u

/** @brief EXTI line 数（pin num 0..15）。 */
#define BM_VENDOR_GPIO_EXTI_LINE_COUNT  16u

/** @brief 未绑定端口标记。 */
#define BM_VENDOR_GPIO_EXTI_PORT_NONE  0xFFu

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

/** @brief 端口到 LL_SYSCFG_EXTI_PORTx 映射。 */
static const uint32_t s_syscfg_ports[BM_VENDOR_GPIO_PORT_COUNT] = {
    LL_SYSCFG_EXTI_PORTA, LL_SYSCFG_EXTI_PORTB, LL_SYSCFG_EXTI_PORTC,
    LL_SYSCFG_EXTI_PORTD, LL_SYSCFG_EXTI_PORTE, LL_SYSCFG_EXTI_PORTF,
    LL_SYSCFG_EXTI_PORTG,
};

/** @brief EXTI line 到 LL_SYSCFG_EXTI_LINEx 映射。 */
static const uint32_t s_syscfg_lines[BM_VENDOR_GPIO_EXTI_LINE_COUNT] = {
    LL_SYSCFG_EXTI_LINE0,  LL_SYSCFG_EXTI_LINE1,  LL_SYSCFG_EXTI_LINE2,
    LL_SYSCFG_EXTI_LINE3,  LL_SYSCFG_EXTI_LINE4,  LL_SYSCFG_EXTI_LINE5,
    LL_SYSCFG_EXTI_LINE6,  LL_SYSCFG_EXTI_LINE7,  LL_SYSCFG_EXTI_LINE8,
    LL_SYSCFG_EXTI_LINE9,  LL_SYSCFG_EXTI_LINE10, LL_SYSCFG_EXTI_LINE11,
    LL_SYSCFG_EXTI_LINE12, LL_SYSCFG_EXTI_LINE13, LL_SYSCFG_EXTI_LINE14,
    LL_SYSCFG_EXTI_LINE15,
};

/** @brief 单条 EXTI line 运行时状态。 */
typedef struct {
    bm_gpio_exti_callback_t cb;
    void                   *user;
    uint32_t                pin;
    uint8_t                 port;
    uint8_t                 enabled;
} bm_vendor_gpio_exti_slot_t;

static bm_vendor_gpio_exti_slot_t s_exti[BM_VENDOR_GPIO_EXTI_LINE_COUNT];

/** @brief 默认 EXTI NVIC 配置（可被应用覆盖后挂到 bm_stm32g4_gpio.config）。 */
static const bm_gpio_stm32g4_config_t g_gpio_cfg_default = {
    .irq_priority = BM_STM32G4_GPIO_EXTI_IRQ_PRIORITY_DEFAULT,
};

/**
 * @brief 解码 pin 并校验端口合法性。
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
 * @brief 由 pin 编码取 EXTI line 号（0..15）。
 */
static int bm_vendor_gpio_exti_line_num(uint32_t pin, uint32_t *line)
{
    uint32_t num = BM_GPIO_PIN_NUM(pin);

    if (BM_GPIO_PIN_PORT(pin) >= BM_VENDOR_GPIO_PORT_COUNT || num >= 16u) {
        return BM_ERR_INVALID;
    }
    *line = num;
    return BM_OK;
}

/**
 * @brief EXTI line 掩码（LL_EXTI_LINE_x = 1<<x）。
 */
static uint32_t bm_vendor_gpio_exti_line_mask(uint32_t line)
{
    return (1u << line);
}

/**
 * @brief 读取 EXTI NVIC 优先级（config 为 NULL 或 0 时用默认）。
 */
static uint32_t bm_vendor_gpio_exti_irq_priority(const struct bm_hal_gpio *dev)
{
    const bm_gpio_stm32g4_config_t *cfg;

    if (dev == NULL || dev->config == NULL) {
        return BM_STM32G4_GPIO_EXTI_IRQ_PRIORITY_DEFAULT;
    }
    cfg = (const bm_gpio_stm32g4_config_t *)dev->config;
    if (cfg->irq_priority == 0u) {
        return BM_STM32G4_GPIO_EXTI_IRQ_PRIORITY_DEFAULT;
    }
    return cfg->irq_priority;
}

/**
 * @brief 由 EXTI line 号取 NVIC 向量。
 */
static IRQn_Type bm_vendor_gpio_exti_irqn(uint32_t line)
{
    if (line < 5u) {
        return (IRQn_Type)((int32_t)EXTI0_IRQn + (int32_t)line);
    }
    if (line < 10u) {
        return EXTI9_5_IRQn;
    }
    return EXTI15_10_IRQn;
}

/**
 * @brief 判断共享/独立 EXTI 向量上是否仍有已注册 line。
 */
static int bm_vendor_gpio_exti_irq_in_use(IRQn_Type irqn)
{
    uint32_t start;
    uint32_t end;
    uint32_t i;

    if (irqn == EXTI9_5_IRQn) {
        start = 5u;
        end   = 9u;
    } else if (irqn == EXTI15_10_IRQn) {
        start = 10u;
        end   = 15u;
    } else if (irqn >= EXTI0_IRQn && irqn <= EXTI4_IRQn) {
        i = (uint32_t)irqn - (uint32_t)EXTI0_IRQn;
        return (s_exti[i].cb != NULL) ? 1 : 0;
    } else {
        return 0;
    }

    for (i = start; i <= end; i++) {
        if (s_exti[i].cb != NULL) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 关闭单条 EXTI line 的边沿与中断屏蔽，并清 pending。
 */
static void bm_vendor_gpio_exti_hw_disable_line(uint32_t line)
{
    uint32_t mask = bm_vendor_gpio_exti_line_mask(line);

    LL_EXTI_DisableRisingTrig_0_31(mask);
    LL_EXTI_DisableFallingTrig_0_31(mask);
    LL_EXTI_DisableIT_0_31(mask);
    LL_EXTI_ClearFlag_0_31(mask);
}

/**
 * @brief 按 flags 配置边沿触发。
 */
static void bm_vendor_gpio_exti_apply_edges(uint32_t line, uint32_t flags)
{
    uint32_t mask = bm_vendor_gpio_exti_line_mask(line);

    LL_EXTI_DisableRisingTrig_0_31(mask);
    LL_EXTI_DisableFallingTrig_0_31(mask);
    if ((flags & BM_GPIO_EXTI_RISING) != 0u) {
        LL_EXTI_EnableRisingTrig_0_31(mask);
    }
    if ((flags & BM_GPIO_EXTI_FALLING) != 0u) {
        LL_EXTI_EnableFallingTrig_0_31(mask);
    }
}

/**
 * @brief 使能 EXTI NVIC（向量级，共享向量仅配置一次）。
 */
static void bm_vendor_gpio_exti_nvic_enable(const struct bm_hal_gpio *dev,
                                            uint32_t line)
{
    IRQn_Type irqn = bm_vendor_gpio_exti_irqn(line);

    NVIC_SetPriority(irqn, bm_vendor_gpio_exti_irq_priority(dev));
    NVIC_EnableIRQ(irqn);
}

/**
 * @brief 若向量上无已注册 line 则关闭 NVIC。
 */
static void bm_vendor_gpio_exti_nvic_disable_if_unused(uint32_t line)
{
    IRQn_Type irqn = bm_vendor_gpio_exti_irqn(line);

    if (bm_vendor_gpio_exti_irq_in_use(irqn) == 0) {
        NVIC_DisableIRQ(irqn);
    }
}

/**
 * @brief EXTI ISR 分发：采样 pending、回调、清标志。
 */
static void bm_vendor_gpio_exti_dispatch(uint32_t line)
{
    uint32_t            mask = bm_vendor_gpio_exti_line_mask(line);
    bm_vendor_gpio_exti_slot_t *slot = &s_exti[line];

    if (LL_EXTI_IsActiveFlag_0_31(mask) == 0u) {
        return;
    }
    LL_EXTI_ClearFlag_0_31(mask);
    if (slot->cb != NULL) {
        slot->cb(slot->pin, slot->user);
    }
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

/**
 * @brief 配置 EXTI（边沿触发 + 回调注册）；cb 为 NULL 时取消注册。
 */
static int bm_vendor_gpio_exti_configure(const struct bm_hal_gpio *dev,
                                         uint32_t pin, uint32_t flags,
                                         bm_gpio_exti_callback_t cb,
                                         void *user)
{
    uint32_t port_idx;
    uint32_t line;
    uint32_t exti_flags;
    bm_vendor_gpio_exti_slot_t *slot;

    if (bm_vendor_gpio_exti_line_num(pin, &line) != BM_OK) {
        return BM_ERR_INVALID;
    }

    port_idx = BM_GPIO_PIN_PORT(pin);
    slot     = &s_exti[line];
    exti_flags = flags & BM_GPIO_EXTI_BOTH;

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_AHB2_GRP1_EnableClock(s_gpio_clk[port_idx]);

    if (cb == NULL) {
        slot->cb      = NULL;
        slot->user    = NULL;
        slot->pin     = 0u;
        slot->port    = BM_VENDOR_GPIO_EXTI_PORT_NONE;
        slot->enabled = 0u;
        bm_vendor_gpio_exti_hw_disable_line(line);
        bm_vendor_gpio_exti_nvic_disable_if_unused(line);
        return BM_OK;
    }

    if (exti_flags == 0u) {
        return BM_ERR_INVALID;
    }

    if (slot->cb != NULL && slot->port != BM_VENDOR_GPIO_EXTI_PORT_NONE
        && slot->port != port_idx) {
        return BM_ERR_BUSY;
    }

    LL_SYSCFG_SetEXTISource(s_syscfg_ports[port_idx], s_syscfg_lines[line]);

    bm_vendor_gpio_exti_apply_edges(line, exti_flags);
    LL_EXTI_EnableIT_0_31(bm_vendor_gpio_exti_line_mask(line));
    LL_EXTI_ClearFlag_0_31(bm_vendor_gpio_exti_line_mask(line));

    slot->cb      = cb;
    slot->user    = user;
    slot->pin     = pin;
    slot->port    = (uint8_t)port_idx;
    slot->enabled = 1u;

    bm_vendor_gpio_exti_nvic_enable(dev, line);
    return BM_OK;
}

/**
 * @brief 使能/禁止 EXTI 中断屏蔽（IMR）。
 */
static int bm_vendor_gpio_exti_enable(const struct bm_hal_gpio *dev,
                                      uint32_t pin, int enable)
{
    uint32_t line;
    uint32_t mask;
    bm_vendor_gpio_exti_slot_t *slot;

    (void)dev;
    if (bm_vendor_gpio_exti_line_num(pin, &line) != BM_OK) {
        return BM_ERR_INVALID;
    }

    slot = &s_exti[line];
    if (slot->cb == NULL) {
        return BM_ERR_INVALID;
    }

    mask = bm_vendor_gpio_exti_line_mask(line);
    if (enable != 0) {
        LL_EXTI_EnableIT_0_31(mask);
        slot->enabled = 1u;
        bm_vendor_gpio_exti_nvic_enable(dev, line);
    } else {
        LL_EXTI_DisableIT_0_31(mask);
        slot->enabled = 0u;
    }
    return BM_OK;
}

/**
 * @brief 清除 EXTI pending（写 PR1）。
 */
static int bm_vendor_gpio_exti_clear_pending(const struct bm_hal_gpio *dev,
                                             uint32_t pin)
{
    uint32_t line;

    (void)dev;
    if (bm_vendor_gpio_exti_line_num(pin, &line) != BM_OK) {
        return BM_ERR_INVALID;
    }
    LL_EXTI_ClearFlag_0_31(bm_vendor_gpio_exti_line_mask(line));
    return BM_OK;
}

/** @brief GPIO 驱动 API 表。 */
static const struct bm_gpio_driver_api g_gpio_api = {
    bm_vendor_gpio_configure,
    bm_vendor_gpio_write,
    bm_vendor_gpio_read,
    bm_vendor_gpio_toggle,
    bm_vendor_gpio_exti_configure,
    bm_vendor_gpio_exti_enable,
    bm_vendor_gpio_exti_clear_pending,
};

/** @brief STM32G4 全芯片 GPIO 设备。 */
const bm_hal_gpio_t bm_stm32g4_gpio = {
    &g_gpio_api,
    &g_gpio_cfg_default,
};

/* -------------------------------------------------------------------------- */
/* EXTI IRQ Handlers（ISR 内仅采样/回调/清 pending）                             */
/* -------------------------------------------------------------------------- */

void EXTI0_IRQHandler(void)  { bm_vendor_gpio_exti_dispatch(0u); }
void EXTI1_IRQHandler(void)  { bm_vendor_gpio_exti_dispatch(1u); }
void EXTI2_IRQHandler(void)  { bm_vendor_gpio_exti_dispatch(2u); }
void EXTI3_IRQHandler(void)  { bm_vendor_gpio_exti_dispatch(3u); }
void EXTI4_IRQHandler(void)  { bm_vendor_gpio_exti_dispatch(4u); }

void EXTI9_5_IRQHandler(void)
{
    uint32_t i;

    for (i = 5u; i <= 9u; i++) {
        bm_vendor_gpio_exti_dispatch(i);
    }
}

void EXTI15_10_IRQHandler(void)
{
    uint32_t i;

    for (i = 10u; i <= 15u; i++) {
        bm_vendor_gpio_exti_dispatch(i);
    }
}

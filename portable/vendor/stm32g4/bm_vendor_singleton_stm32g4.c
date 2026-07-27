/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_singleton_stm32g4.c
 * @brief STM32G474xB 后端的 timer / UART / WDG 单例与 uptime 单调时钟实现
 *
 * 外设驱动经 STM32 LL 库（stm32g4xx_ll_*.h）实现，不引 Cube HAL（刻意不定义
 * USE_HAL_DRIVER；LL 独立可用）。全部实例绑定（外设/GPIO/通道）经
 * bm_hal_instances_stm32g4.h 的可覆盖宏获取，本文件不写死板级常量。
 *
 * 组成：
 *   - bm_drv_timer_api：TIM6（可切 TIM7）周期 update 中断，ISR 内派发 tick
 *     回调，派发前后加 bm_arch_isr_fpu_enter/exit 守卫（armv7em 上 no-op）。
 *   - 默认控制台 UART 设备 bm_uart_default：LPUART1（ST-LINK VCP）
 *     轮询收发 + RX 中断回调（统一实例模型，见 bm_hal_uart.h）。
 *   - bm_drv_wdg_api：IWDG（独立 LSI ~32kHz）。
 *   - bm_hal_uptime_ns_raw()：DWT CYCCNT @170MHz，32 位计数器溢出做 64 位扩展
 *     （CMSIS Core 原语，不属外设寄存器、LL 不覆盖，保留 CMSIS 写法）。
 *
 * 时钟假设：vendor 不接管时钟树（SystemInit 由 Cube system_stm32g4xx.c 提供），
 * 总线时钟经 LL_RCC_GetSystemClocksFreq() 按现行分频实时推算，定时器时钟
 * 按 RM0440 “APB 分频 >1 时倍频”规则折算；应用改时钟树后重新 init 即可。
 *
 * 保留 CMSIS 写法的位置（LL 无对应 API，逐处注释）：
 *   - NVIC 优先级/使能（LL 无 NVIC 抽象，用 CMSIS core 的 NVIC_* 函数）；
 *   - DWT CYCCNT 时间基（CoreDebug/DWT 属 CMSIS Core 外设，LL 不覆盖）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（STM32G474xB 移植）
 * 2026-07-27       1.1            zeh            寄存器级改写为 STM32 LL 库实现（决策变更：提高可读性）
 * 2026-07-27       1.2            zeh            UART 统一实例化：单例 bm_drv_uart_api 改为
 *                                                默认控制台设备 bm_uart_default（统一实例模型）
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_hal_uart.h"
#include "bm_drv_wdg.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm_hal_uptime.h"
#include "bm_types.h"
#include "armv7em/bm_arch_isr_fpu.h"

#include <stddef.h>
#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_tim.h"
#include "stm32g4xx_ll_lpuart.h"
#include "stm32g4xx_ll_iwdg.h"

/* ---------- tick 定时器实例选择（TIM6 默认 / TIM7 备选，寄存器布局一致） ---------- */

#if defined(BM_STM32G4_TICK_USE_TIM7)
#define BM_TICK_TIM       TIM7
#define BM_TICK_IRQn      TIM7_IRQn
#define BM_TICK_RCC_EN    LL_APB1_GRP1_PERIPH_TIM7
#define BM_TICK_IRQHandler TIM7_IRQHandler
#else
#define BM_TICK_TIM       TIM6
#define BM_TICK_IRQn      TIM6_DAC_IRQn
#define BM_TICK_RCC_EN    LL_APB1_GRP1_PERIPH_TIM6
#define BM_TICK_IRQHandler TIM6_DAC_IRQHandler
#endif

/* ---------- 全局状态 ---------- */
static void (*g_tick_callback)(void);
static void (*g_rx_callback)(uint8_t c);
static uint32_t g_tick_freq_hz;
static uint32_t g_tick_count;
static uint8_t  g_uart_ready;
static uint8_t  g_wdg_ready;

/**
 * @brief tick ISR FPU 现场保存区占位（armv7em 上守卫为 no-op，仅占位）。
 *
 * 对齐 esp32 单例的调用形态：回调派发链（可能触达电流环浮点回调）整体包在
 * bm_arch_isr_fpu_enter/exit 之间，Cortex-M4 硬件自动/lazy stacking 承担
 * 浮点现场保存，守卫零成本。
 */
static uint8_t g_tick_fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));

/* ---------- 时钟推算辅助 ---------- */

/**
 * @brief 查询 APB1 总线时钟（PCLK1，Hz），按 RCC 现行分频实时推算。
 * @return PCLK1 频率（Hz）。
 */
static uint32_t bm_stm32g4_pclk1_hz(void)
{
    LL_RCC_ClocksTypeDef clocks;

    LL_RCC_GetSystemClocksFreq(&clocks);
    return clocks.PCLK1_Frequency;
}

/**
 * @brief APB1 定时器时钟（APB1 分频 >1 时定时器时钟倍频，RM0440 规则）。
 * @return APB1 定时器时钟（Hz）。
 */
static uint32_t bm_stm32g4_apb1_tim_hz(void)
{
    uint32_t pclk1 = bm_stm32g4_pclk1_hz();

    return (LL_RCC_GetAPB1Prescaler() == LL_RCC_APB1_DIV_1) ? pclk1
                                                            : (pclk1 * 2u);
}

/**
 * @brief GPIO 复用功能配置（推挽、高速、无上下拉）。
 * @param port GPIO 端口。
 * @param pin  引脚号（0..15）。
 * @param af   复用功能号（0..15）。
 */
static void bm_stm32g4_gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
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

/* ---------- Timer ISR ---------- */

/**
 * @brief tick 定时器 update ISR。
 *
 * 铁律：先清 UIF、再累加 tick 计数（不受浮点开销影响），最后在 FPU 守卫内
 * 派发用户回调（对齐 esp32 单例 bm_vendor_tick_isr 的顺序约定）。
 */
void BM_TICK_IRQHandler(void)
{
    unsigned fpu_prev;

    if (LL_TIM_IsActiveFlag_UPDATE(BM_TICK_TIM) == 0u) {
        return;
    }
    LL_TIM_ClearFlag_UPDATE(BM_TICK_TIM);
    g_tick_count++;

    fpu_prev = bm_arch_isr_fpu_enter(g_tick_fpu_sa);
    if (g_tick_callback != NULL) {
        g_tick_callback();
    }
    bm_arch_isr_fpu_exit(g_tick_fpu_sa, fpu_prev);
}

/* ---------- timer 驱动实现 ---------- */

/**
 * @brief 初始化系统 tick 定时器（周期 update 中断）。
 *
 * 按 APB1 定时器时钟实时推算分频：自动选 PSC 使 ARR 落入 16 位量程。
 *
 * @param freq_hz tick 频率（Hz）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或频率超量程。
 */
static int stm32g4_timer_init(uint32_t freq_hz)
{
    uint64_t ticks;
    uint32_t psc;
    uint32_t arr;
    uint32_t tim_clk = bm_stm32g4_apb1_tim_hz();

    if (freq_hz == 0u || freq_hz > (tim_clk / 2u)) {
        return BM_ERR_INVALID;
    }

    ticks = tim_clk / freq_hz;
    psc   = (uint32_t)((ticks + 65535ull) / 65536ull) - 1u;
    arr   = (uint32_t)(ticks / (psc + 1u)) - 1u;

    LL_APB1_GRP1_EnableClock(BM_TICK_RCC_EN);

    LL_TIM_DisableCounter(BM_TICK_TIM);
    LL_TIM_SetPrescaler(BM_TICK_TIM, psc);
    LL_TIM_SetAutoReload(BM_TICK_TIM, arr);
    LL_TIM_ClearFlag_UPDATE(BM_TICK_TIM);  /* 清遗留标志 */
    LL_TIM_EnableIT_UPDATE(BM_TICK_TIM);
    LL_TIM_GenerateEvent_UPDATE(BM_TICK_TIM); /* 装载 PSC/ARR 预装值 */

    /* NVIC 无 LL API（LL 不抽象中断控制器），用 CMSIS core 函数 */
    NVIC_SetPriority(BM_TICK_IRQn, BM_STM32G4_TICK_IRQ_PRIORITY);
    NVIC_EnableIRQ(BM_TICK_IRQn);

    g_tick_freq_hz = freq_hz;
    g_tick_count   = 0u;
    LL_TIM_EnableCounter(BM_TICK_TIM);
    return BM_OK;
}

/**
 * @brief 停止系统 tick 定时器并撤销回调。
 */
static void stm32g4_timer_stop(void)
{
    LL_TIM_DisableCounter(BM_TICK_TIM);
    LL_TIM_DisableIT_UPDATE(BM_TICK_TIM);
    NVIC_DisableIRQ(BM_TICK_IRQn); /* NVIC 无 LL API，同上 */

    g_tick_callback = NULL;
    g_tick_freq_hz  = 0u;
    g_tick_count    = 0u;
}

/**
 * @brief 读取当前 tick 计数（自由运行，每 ISR +1）。
 * @return 当前 tick 计数。
 */
static uint32_t stm32g4_timer_get_ticks(void)
{
    return g_tick_count;
}

/**
 * @brief 查询 tick 频率。
 * @return 当前配置的 tick 频率（Hz）。
 */
static uint32_t stm32g4_timer_get_freq(void)
{
    return g_tick_freq_hz;
}

/**
 * @brief 注册 tick 回调（ISR 上下文按 freq_hz 周期调用）。
 * @param cb tick 回调；NULL 取消注册。
 */
static void stm32g4_timer_set_callback(void (*cb)(void))
{
    g_tick_callback = cb;
}

/** @brief timer 驱动 API 表（bm_hal 分发层 extern 消费）。 */
const struct bm_timer_driver_api bm_drv_timer_api = {
    stm32g4_timer_init,
    stm32g4_timer_stop,
    stm32g4_timer_get_ticks,
    stm32g4_timer_get_freq,
    stm32g4_timer_set_callback,
};

/* ---------- UART 驱动实现（LPUART1，轮询收发 + RX 中断回调） ---------- */

/**
 * @brief 初始化 console UART（LPUART1 @ PA2/PA3，引脚/波特率走 instances 宏）。
 *
 * 波特率经 LL_LPUART_SetBaudRate 按 PCLK1 推算（LPUART 时钟源取复位默认
 * PCLK1，即 CCIPR.LPUART1SEL=0；改过时钟源选择的板级须实机核对）。
 *
 * @param config 未使用（NULL）。
 * @return BM_OK。
 */
static int stm32g4_uart_init(const struct bm_hal_uart *dev, void *config)
{
    (void)dev;
    (void)config;

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_LPUART1);

    bm_stm32g4_gpio_af(GPIOA, BM_STM32G4_UART_TX_PIN, BM_STM32G4_UART_GPIO_AF);
    bm_stm32g4_gpio_af(GPIOA, BM_STM32G4_UART_RX_PIN, BM_STM32G4_UART_GPIO_AF);

    LL_LPUART_Disable(LPUART1);
    LL_LPUART_SetBaudRate(LPUART1, bm_stm32g4_pclk1_hz(),
                          LL_LPUART_PRESCALER_DIV1, BM_STM32G4_UART_BAUD);
    LL_LPUART_EnableDirectionTx(LPUART1);
    LL_LPUART_EnableDirectionRx(LPUART1);
    LL_LPUART_Enable(LPUART1);

    g_uart_ready = 1u;
    return BM_OK;
}

/**
 * @brief 发送字节流（轮询 TXE，对齐 console 写路径语义）。
 * @param data 数据指针。
 * @param len  数据长度。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未初始化。
 */
static int stm32g4_uart_send(const struct bm_hal_uart *dev,
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
        while (LL_LPUART_IsActiveFlag_TXE(LPUART1) == 0u) {
        }
        LL_LPUART_TransmitData8(LPUART1, data[i]);
    }
    return BM_OK;
}

/**
 * @brief 非阻塞轮询接收：RXNE 有数据则读出，无数据立即返回 0。
 * @param data    接收缓冲区。
 * @param max_len 缓冲区容量（字节）。
 * @return 实际读出的字节数；无数据/未初始化/参数无效时为 0。
 */
static size_t stm32g4_uart_recv(const struct bm_hal_uart *dev,
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
    while (n < max_len && LL_LPUART_IsActiveFlag_RXNE(LPUART1) != 0u) {
        data[n++] = LL_LPUART_ReceiveData8(LPUART1);
    }
    return n;
}

/**
 * @brief LPUART1 RX 中断：读 RDR 清 RXNE，逐字节派发注册的 RX 回调。
 */
void LPUART1_IRQHandler(void)
{
    while (LL_LPUART_IsActiveFlag_RXNE(LPUART1) != 0u) {
        uint8_t c = LL_LPUART_ReceiveData8(LPUART1);
        if (g_rx_callback != NULL) {
            g_rx_callback(c);
        }
    }
}

/**
 * @brief 注册 RX 回调；设置时打开 RXNE 中断源，NULL 时先关中断源再清回调。
 * @param cb RX 回调；NULL 取消注册。
 */
static void stm32g4_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                         void (*cb)(uint8_t c))
{
    (void)dev;
    if (cb == NULL) {
        LL_LPUART_DisableIT_RXNE_RXFNE(LPUART1);
        NVIC_DisableIRQ(LPUART1_IRQn); /* NVIC 无 LL API，用 CMSIS core 函数 */
        g_rx_callback = NULL;
        return;
    }
    g_rx_callback = cb;
    LL_LPUART_EnableIT_RXNE_RXFNE(LPUART1);
    NVIC_SetPriority(LPUART1_IRQn, BM_STM32G4_UART_IRQ_PRIORITY);
    NVIC_EnableIRQ(LPUART1_IRQn);
}

/** @brief UART 驱动 API 表。 */
static const struct bm_uart_driver_api g_uart_api = {
    stm32g4_uart_init,
    stm32g4_uart_send,
    stm32g4_uart_recv,
    stm32g4_uart_set_rx_callback,
};

/** @brief 默认控制台 UART 设备（LPUART1，统一实例模型，见 bm_hal_uart.h）。 */
const bm_hal_uart_t bm_uart_default = { &g_uart_api, NULL };

/* ---------- WDG 驱动实现（IWDG，独立 LSI ~32kHz） ---------- */

/** @brief IWDG 分频档编码表（index i ↔ 分频 4×2^i）。 */
static const uint32_t s_iwdg_prescaler_ll[7] = {
    LL_IWDG_PRESCALER_4,   LL_IWDG_PRESCALER_8,   LL_IWDG_PRESCALER_16,
    LL_IWDG_PRESCALER_32,  LL_IWDG_PRESCALER_64,  LL_IWDG_PRESCALER_128,
    LL_IWDG_PRESCALER_256,
};

/**
 * @brief 初始化并启动 IWDG。
 *
 * LSI 未开则先开并等待就绪；按 timeout_ms 自动选分频（4..256）使重装载值
 * 落入 12 位量程。IWDG 一旦启动不可停（硬件特性），feed 为唯一维持手段。
 *
 * @param timeout_ms 超时时间（ms）；0 时默认 5000 ms。
 * @return BM_OK 成功；BM_ERR_INVALID 超时时长超出可编码范围。
 */
static int stm32g4_wdg_init(uint32_t timeout_ms)
{
    uint32_t ticks32k;
    uint32_t i;
    uint32_t rlr = 0u;

    if (timeout_ms == 0u) {
        timeout_ms = 5000u;
    }
    ticks32k = (BM_STM32G4_LSI_HZ / 1000u) * timeout_ms;

    /* 选最小分频使 rlr <= 4096（分频档 4×2^i，i=0..6） */
    for (i = 0u; i < 7u; ++i) {
        uint32_t div = 4u << i;
        rlr = ticks32k / div;
        if (rlr >= 1u && rlr <= 4096u) {
            break;
        }
    }
    if (i >= 7u) {
        return BM_ERR_INVALID;
    }
    if (rlr > 4096u) {
        rlr = 4096u;
    }

    if (LL_RCC_LSI_IsReady() == 0u) {
        LL_RCC_LSI_Enable();
        while (LL_RCC_LSI_IsReady() == 0u) {
        }
    }

    LL_IWDG_EnableWriteAccess(IWDG);
    LL_IWDG_SetPrescaler(IWDG, s_iwdg_prescaler_ll[i]);
    LL_IWDG_SetReloadCounter(IWDG, rlr - 1u);
    while (LL_IWDG_IsActiveFlag_PVU(IWDG) != 0u
           || LL_IWDG_IsActiveFlag_RVU(IWDG) != 0u) {
    }
    LL_IWDG_ReloadCounter(IWDG);
    LL_IWDG_Enable(IWDG);

    g_wdg_ready = 1u;
    return BM_OK;
}

/**
 * @brief 喂狗（重装载 IWDG 计数器）；未初始化时静默返回。
 */
static void stm32g4_wdg_feed(void)
{
    if (g_wdg_ready == 0u) {
        return;
    }
    LL_IWDG_ReloadCounter(IWDG);
}

/** @brief WDG 驱动 API 表。 */
const struct bm_wdg_driver_api bm_drv_wdg_api = {
    stm32g4_wdg_init,
    stm32g4_wdg_feed,
};

/* ---------- 单调时钟后端（DWT CYCCNT @170MHz，64 位扩展） ---------- */

/**
 * @brief STM32G4 单调时钟后端（DWT CYCCNT）。
 *
 * 首次调用时懒初始化：CoreDebug.DEMCR.TRCENA + DWT.CTRL.CYCCNTENA，
 * 无需应用显式初始化（契约见 bm_hal_uptime.h）。
 * CoreDebug/DWT 属 CMSIS Core 外设、LL 不覆盖，此处保留 CMSIS 写法。
 *
 * CYCCNT 为 32 位自由运行计数器（170MHz 下约 25.2s 回绕），此处按
 * “回绕即高位 +1”做 64 位扩展——前提为读取间隔小于一个回绕周期
 * （tick/控制环周期 µs~ms 级，恒满足）。
 *
 * 纳秒换算避免 cycles×1e9 溢出 64 位（170MHz 下约 108s 即溢出）：
 * 拆为 (cycles/f)×1e9 + (cycles%f)×1e9/f 两段，商余路径各自有界。
 *
 * @return 自 DWT 使能起经过的纳秒数（uint64_t，单调不减）。
 */
uint64_t bm_hal_uptime_ns_raw(void)
{
    static uint32_t s_last;
    static uint32_t s_high;
    static uint8_t  s_dwt_ready;
    uint32_t cycles;
    uint64_t total;
    uint64_t freq = SystemCoreClock;

    if (s_dwt_ready == 0u) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0u;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
        s_last = 0u;
        s_high = 0u;
        s_dwt_ready = 1u;
    }

    cycles = DWT->CYCCNT;
    if (cycles < s_last) {
        s_high++;
    }
    s_last = cycles;

    total = ((uint64_t)s_high << 32) | cycles;
    return (total / freq) * 1000000000ull + (total % freq) * 1000000000ull / freq;
}

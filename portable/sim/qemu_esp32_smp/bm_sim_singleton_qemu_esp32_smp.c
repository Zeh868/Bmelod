/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_esp32_smp.c
 * @brief QEMU ESP32 Xtensa SMP 单例驱动（TIMG0 定时器 / UART0 / 看门狗桩）
 *
 * 临界区与内存屏障由 `bm_port_arch_xtensa` 提供。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_log.h"
#include "bm_types.h"
#include "hal/bm_hal_cpu.h"

#include <stddef.h>
#include <stdint.h>

#define TAG_TIMER "hal_timer"
#define TAG_UART  "hal_uart"
#define TAG_WDG   "hal_wdg"

/** ESP32 Timer Group 0 */
#define TIMG0_BASE          0x3FF5F000UL
#define TIMG0_T0CONFIG      (*(volatile uint32_t *)(TIMG0_BASE + 0x000))
#define TIMG0_T0LO           (*(volatile uint32_t *)(TIMG0_BASE + 0x004))
#define TIMG0_T0HI           (*(volatile uint32_t *)(TIMG0_BASE + 0x008))
#define TIMG0_T0UPDATE       (*(volatile uint32_t *)(TIMG0_BASE + 0x00C))
#define TIMG0_T0ALARMLO      (*(volatile uint32_t *)(TIMG0_BASE + 0x010))
#define TIMG0_T0ALARMHI      (*(volatile uint32_t *)(TIMG0_BASE + 0x014))
#define TIMG0_T0LOADLO       (*(volatile uint32_t *)(TIMG0_BASE + 0x018))
#define TIMG0_T0LOADHI       (*(volatile uint32_t *)(TIMG0_BASE + 0x01C))
#define TIMG0_T0LOAD         (*(volatile uint32_t *)(TIMG0_BASE + 0x020))
#define TIMG0_INT_ENA        (*(volatile uint32_t *)(TIMG0_BASE + 0x098))
#define TIMG0_INT_RAW        (*(volatile uint32_t *)(TIMG0_BASE + 0x09C))
#define TIMG0_INT_CLR        (*(volatile uint32_t *)(TIMG0_BASE + 0x0A4))

/** T0CONFIG 位域 */
#define TIMG_T0_EN           (1u << 30)
#define TIMG_T0_INCREASE     (1u << 29)
#define TIMG_T0_AUTORELOAD   (1u << 28)
#define TIMG_T0_DIVIDER_EN   (1u << 13)
#define TIMG_T0_ALARM_EN     (1u << 31)

/** ESP32 UART0 */
#define UART0_BASE           0x3FF40000UL
#define UART0_FIFO           (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART0_STATUS         (*(volatile uint32_t *)(UART0_BASE + 0x01C))

/** STATUS bit16:24 = TXFIFO_CNT */
#define UART0_TXFIFO_CNT(status) (((status) >> 16) & 0xFFu)
#define UART0_TXFIFO_MAX       128u

/** QEMU ESP32 APB 定时器时钟（80 MHz） */
#define ESP32_SMP_TIMER_HZ   80000000u
/** TIMG0 T0 电平中断源号 */
#define ESP32_TG0_T0_INT_NUM 6u

static uint32_t g_tick_freq[BM_CONFIG_CPU_COUNT];
static volatile uint32_t g_ticks[BM_CONFIG_CPU_COUNT];
static void (*g_tick_cb[BM_CONFIG_CPU_COUNT])(void);
static uint32_t g_timer_alarm_ticks[BM_CONFIG_CPU_COUNT];
static int g_timer_armed[BM_CONFIG_CPU_COUNT];

/** PRO_CPU 拥有 TIMG0；从核共享 cpu0 tick */
#define ESP32_SMP_TIMER_OWNER_CPU  0u

/**
 * @brief 返回当前 CPU 索引（越界时回落 0）
 */
static uint32_t esp32_smp_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

/**
 * @brief 使能 CPU 侧定时器中断（仅 PRO_CPU 操作硬件）
 */
static void esp32_smp_timer_enable_irq(void) {
    uint32_t ie;

    if (esp32_smp_cpu_index() != ESP32_SMP_TIMER_OWNER_CPU) {
        return;
    }
    __asm__ volatile("rsr.intenable %0" : "=a"(ie));
    ie |= (1u << ESP32_TG0_T0_INT_NUM);
    __asm__ volatile("wsr.intenable %0" :: "a"(ie));
}

/**
 * @brief 装载 TIMG0 T0 下一次 alarm（仅 PRO_CPU 写硬件）
 */
static void esp32_smp_timer_arm(uint32_t cpu) {
    uint32_t alarm = g_timer_alarm_ticks[cpu];

    if (cpu != ESP32_SMP_TIMER_OWNER_CPU) {
        return;
    }
    TIMG0_T0ALARMLO = alarm;
    TIMG0_T0ALARMHI = 0u;
    TIMG0_T0LOAD = 1u;
    g_timer_armed[cpu] = 1;
    TIMG0_INT_ENA = 1u;
    esp32_smp_timer_enable_irq();
}

static int esp32_smp_timer_init(uint32_t freq_hz) {
    uint32_t cpu = esp32_smp_cpu_index();
    uint32_t hz = (freq_hz > 0u) ? freq_hz : 1000u;
    uint32_t divider = 80u;

    g_tick_freq[cpu] = hz;
    g_ticks[cpu] = 0u;
    g_timer_alarm_ticks[cpu] = ESP32_SMP_TIMER_HZ / divider / hz;
    if (g_timer_alarm_ticks[cpu] == 0u) {
        g_timer_alarm_ticks[cpu] = 1u;
    }

    /* APP_CPU 不操作 TIMG0；tick 由 PRO_CPU 中断驱动并镜像 */
    if (cpu != 0u) {
        BM_LOGI(TAG_TIMER, "init: cpu=%u freq_hz=%u (shared tick)", (unsigned)cpu, hz);
        return BM_OK;
    }

    TIMG0_T0CONFIG = 0u;
    TIMG0_T0LO = 0u;
    TIMG0_T0HI = 0u;
    TIMG0_T0UPDATE = 1u;
    TIMG0_INT_CLR = 1u;

    TIMG0_T0CONFIG = TIMG_T0_EN | TIMG_T0_INCREASE | TIMG_T0_AUTORELOAD
                     | TIMG_T0_DIVIDER_EN | TIMG_T0_ALARM_EN
                     | ((divider - 1u) & 0x1FFFu);

    esp32_smp_timer_arm(cpu);
    BM_LOGI(TAG_TIMER, "init: cpu=%u freq_hz=%u", (unsigned)cpu, hz);
    return BM_OK;
}

static void esp32_smp_timer_stop(void) {
    uint32_t cpu = esp32_smp_cpu_index();

    g_tick_cb[cpu] = NULL;
    g_timer_armed[cpu] = 0;
    if (cpu != 0u) {
        return;
    }
    TIMG0_INT_ENA = 0u;
    TIMG0_T0CONFIG = 0u;
}

static uint32_t esp32_smp_timer_get_ticks(void) {
    uint32_t cpu = esp32_smp_cpu_index();

    /* APP_CPU 读取 PRO_CPU 驱动的共享 tick */
    if (cpu != 0u) {
        return g_ticks[0];
    }
    return g_ticks[0];
}

static uint32_t esp32_smp_timer_get_freq(void) {
    return g_tick_freq[esp32_smp_cpu_index()];
}

static void esp32_smp_timer_set_callback(void (*cb)(void)) {
    g_tick_cb[esp32_smp_cpu_index()] = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    esp32_smp_timer_init,
    esp32_smp_timer_stop,
    esp32_smp_timer_get_ticks,
    esp32_smp_timer_get_freq,
    esp32_smp_timer_set_callback,
};

/**
 * @brief TIMG0 T0 电平中断服务（由 Level-1 向量调用，仅 PRO_CPU 硬件路径）
 */
void qemu_esp32_smp_on_timer_irq(void) {
    uint32_t cpu;
    uint32_t n;
    void (*cb)(void);

    if (esp32_smp_cpu_index() != 0u) {
        return;
    }
    if ((TIMG0_INT_RAW & 1u) == 0u) {
        return;
    }
    if (!g_timer_armed[0]) {
        TIMG0_INT_CLR = 1u;
        return;
    }
    TIMG0_INT_CLR = 1u;
    g_ticks[0]++;
    for (n = 1u; n < BM_CONFIG_CPU_COUNT; n++) {
        g_ticks[n] = g_ticks[0];
    }
    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        cb = g_tick_cb[cpu];
        if (cb) {
            cb();
        }
    }
    esp32_smp_timer_arm(0u);
}

static int esp32_smp_uart_init(void *config) {
    (void)config;
    BM_LOGI(TAG_UART, "init: UART0");
    return BM_OK;
}

static int esp32_smp_uart_send(const uint8_t *data, size_t len) {
    size_t i;

    if (!data || len == 0u) {
        return BM_OK;
    }
    for (i = 0u; i < len; i++) {
        while (UART0_TXFIFO_CNT(UART0_STATUS) >= UART0_TXFIFO_MAX) {
        }
        UART0_FIFO = (uint32_t)data[i];
    }
    return BM_OK;
}

static size_t esp32_smp_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void esp32_smp_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    esp32_smp_uart_init,
    esp32_smp_uart_send,
    esp32_smp_uart_recv,
    esp32_smp_uart_set_rx_callback,
};

static int esp32_smp_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

static void esp32_smp_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    esp32_smp_wdg_init,
    esp32_smp_wdg_feed,
};

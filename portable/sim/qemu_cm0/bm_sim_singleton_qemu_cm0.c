/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_cm0.c
 * @brief QEMU Cortex-M0 仿真单例驱动（定时器 / UART / 看门狗 / 单调时钟）
 *
 * 临界区与内存屏障由 `bm_port_arch_armv6m` 提供。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            从 qemu_cortex_m0 singleton 拆分
 * 2026-06-26       1.1            zeh            添加 bm_hal_uptime_ns_raw()（路线图 #9 时间基统一 1a）
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_log.h"
#include "bm_types.h"
#include "hal/bm_hal_uptime.h"

#include <stddef.h>
#include <stdint.h>

#define TAG_TIMER "hal_timer"
#define TAG_UART  "hal_uart"
#define TAG_WDG   "hal_wdg"

#define TIMER1_BASE             0x40009000U
#define TIMER1_TASKS_START      (*(volatile uint32_t *)(TIMER1_BASE + 0x000U))
#define TIMER1_TASKS_STOP       (*(volatile uint32_t *)(TIMER1_BASE + 0x004U))
#define TIMER1_TASKS_CLEAR      (*(volatile uint32_t *)(TIMER1_BASE + 0x00CU))
#define TIMER1_EVENTS_COMPARE0  (*(volatile uint32_t *)(TIMER1_BASE + 0x140U))
#define TIMER1_MODE             (*(volatile uint32_t *)(TIMER1_BASE + 0x504U))
#define TIMER1_BITMODE          (*(volatile uint32_t *)(TIMER1_BASE + 0x508U))
#define TIMER1_PRESCALER        (*(volatile uint32_t *)(TIMER1_BASE + 0x510U))
#define TIMER1_CC0              (*(volatile uint32_t *)(TIMER1_BASE + 0x540U))
#define TIMER1_INTENSET         (*(volatile uint32_t *)(TIMER1_BASE + 0x304U))
#define TIMER1_INTENCLR         (*(volatile uint32_t *)(TIMER1_BASE + 0x308U))
#define NVIC_ISER               (*(volatile uint32_t *)0xE000E100U)
#define NVIC_ICPR               (*(volatile uint32_t *)0xE000E280U)
#define TIMER1_IRQ_NUMBER       9U
#define NRF_TIMER_CLK_HZ        1000000u

static volatile uint32_t g_qemu_ticks;
static void (*g_qemu_tick_cb)(void);
static uint32_t g_qemu_tick_freq = 1000u;

static void qemu_timer_program_period(void) {
    uint32_t cc = NRF_TIMER_CLK_HZ / g_qemu_tick_freq;
    if (cc == 0u) {
        cc = 1u;
    }
    TIMER1_CC0 = cc;
}

void TIMER1_IRQHandler(void) {
    if (TIMER1_EVENTS_COMPARE0 == 0U) {
        return;
    }
    TIMER1_EVENTS_COMPARE0 = 0U;
    TIMER1_TASKS_CLEAR = 1U;
    g_qemu_ticks++;
    if (g_qemu_tick_cb) {
        g_qemu_tick_cb();
    }
    TIMER1_TASKS_START = 1U;
}

static int qemu_timer_init(uint32_t freq_hz) {
    g_qemu_tick_freq = (freq_hz > 0u) ? freq_hz : 1000u;
    TIMER1_TASKS_STOP = 1U;
    TIMER1_TASKS_CLEAR = 1U;
    TIMER1_MODE = 0U;
    TIMER1_BITMODE = 0U;
    TIMER1_PRESCALER = 4U;
    qemu_timer_program_period();
    TIMER1_INTENCLR = 0xFFFFFFFFU;
    TIMER1_INTENSET = (1U << 16);
    TIMER1_EVENTS_COMPARE0 = 0U;
    NVIC_ICPR = (1U << TIMER1_IRQ_NUMBER);
    NVIC_ISER = (1U << TIMER1_IRQ_NUMBER);
    TIMER1_TASKS_START = 1U;
    BM_LOGI(TAG_TIMER, "init: freq_hz=%u", g_qemu_tick_freq);
    return BM_OK;
}

static void qemu_timer_stop(void) {
    TIMER1_TASKS_STOP = 1U;
    TIMER1_INTENCLR = (1U << 16);
    g_qemu_tick_cb = NULL;
    BM_LOGI(TAG_TIMER, "stop");
}

static uint32_t qemu_timer_get_ticks(void) {
    return g_qemu_ticks;
}

static uint32_t qemu_timer_get_freq(void) {
    return g_qemu_tick_freq;
}

static void qemu_timer_set_callback(void (*cb)(void)) {
    g_qemu_tick_cb = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    qemu_timer_init,
    qemu_timer_stop,
    qemu_timer_get_ticks,
    qemu_timer_get_freq,
    qemu_timer_set_callback,
};

static void qemu_semihosting_write(const uint8_t *data, size_t len) {
    (void)len;
    __asm volatile (
        "movs r0, #0x04\n"
        "movs r1, %0\n"
        "bkpt 0xAB\n"
        :
        : "r"(data)
        : "r0", "r1", "memory"
    );
}

static int qemu_uart_init(void *config) {
    (void)config;
    BM_LOGI(TAG_UART, "init: semihosting backend");
    return BM_OK;
}

static int qemu_uart_send(const uint8_t *data, size_t len) {
    qemu_semihosting_write(data, len);
    return BM_OK;
}

static size_t qemu_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void qemu_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    qemu_uart_init,
    qemu_uart_send,
    qemu_uart_recv,
    qemu_uart_set_rx_callback,
};

static int qemu_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: timeout_ms=%u (stub)", timeout_ms);
    return BM_OK;
}

static void qemu_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    qemu_wdg_init,
    qemu_wdg_feed,
};

/**
 * @brief QEMU Cortex-M0 单调时钟后端（tick 计数器换算）
 *
 * 以 TIMER1 中断计数（g_qemu_ticks）除以 tick 频率换算为纳秒。
 * 需先调用 bm_hal_timer_init() 启动 TIMER1；初始化前返回 0。
 *
 * @return 自 TIMER1 初始化起经过的纳秒数（uint64_t，单调不减）
 */
uint64_t bm_hal_uptime_ns_raw(void) {
    uint64_t ticks = (uint64_t)g_qemu_ticks;
    uint64_t freq  = (g_qemu_tick_freq > 0u) ? (uint64_t)g_qemu_tick_freq : 1000u;

    /* 拆分避免 ticks * 1e9 中间溢出（tick 值通常很小，此处仍防御性拆分）*/
    return (ticks / freq) * 1000000000u
         + (ticks % freq) * 1000000000u / freq;
}

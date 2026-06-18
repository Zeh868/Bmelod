/**
 * @file bm_sim_singleton_qemu_riscv64.c
 * @brief QEMU RISC-V64 virt 仿真单例驱动（定时器 / UART / 看门狗桩）
 *
 * 临界区与内存屏障由 `bm_port_arch_riscv64` 提供；UART 使用 OpenSBI/SBI 桩或空操作。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_log.h"
#include "bm_types.h"

#include <stdint.h>

#define TAG_TIMER "hal_timer"
#define TAG_UART  "hal_uart"
#define TAG_WDG   "hal_wdg"

static uint32_t g_tick_freq = 1000u;
static volatile uint32_t g_ticks;
static void (*g_tick_cb)(void);

static int rv64_timer_init(uint32_t freq_hz) {
    g_tick_freq = (freq_hz > 0u) ? freq_hz : 1000u;
    g_ticks = 0u;
    BM_LOGI(TAG_TIMER, "init: freq_hz=%u (stub)", g_tick_freq);
    return BM_OK;
}

static void rv64_timer_stop(void) {
    g_tick_cb = NULL;
}

static uint32_t rv64_timer_get_ticks(void) {
    return g_ticks;
}

static uint32_t rv64_timer_get_freq(void) {
    return g_tick_freq;
}

static void rv64_timer_set_callback(void (*cb)(void)) {
    g_tick_cb = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    rv64_timer_init,
    rv64_timer_stop,
    rv64_timer_get_ticks,
    rv64_timer_get_freq,
    rv64_timer_set_callback,
};

static int rv64_uart_init(void *config) {
    (void)config;
    BM_LOGI(TAG_UART, "init: virt uart stub");
    return BM_OK;
}

static int rv64_uart_send(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return BM_OK;
}

static size_t rv64_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void rv64_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    rv64_uart_init,
    rv64_uart_send,
    rv64_uart_recv,
    rv64_uart_set_rx_callback,
};

static int rv64_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

static void rv64_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    rv64_wdg_init,
    rv64_wdg_feed,
};

/**
 * @file bm_sim_singleton_native.c
 * @brief native_sim 单例驱动（定时器 / UART / 看门狗）
 *
 * 临界区与屏障由所选 arch/backend 实现提供。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            临界区拆至 arch/host 与 MP 专用文件
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_hal_timer_native.h"
#include "bm_hal_wdg_native.h"
#include "bm_sim_native_internal.h"
#include "bm_config.h"
#include "hal/bm_hal_cpu.h"

#include <stdio.h>
#include <stdint.h>

volatile uint8_t g_sim_native_isr_depth[BM_CONFIG_CPU_COUNT];

static uint32_t sim_native_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

/* --- timer --- */
static uint32_t g_tick_freq[BM_CONFIG_CPU_COUNT];
static uint32_t g_tick_count[BM_CONFIG_CPU_COUNT];
static void (*g_tick_callback[BM_CONFIG_CPU_COUNT])(void);
static int g_timer_init_result[BM_CONFIG_CPU_COUNT];

static int native_timer_init(uint32_t freq_hz) {
    uint32_t cpu = sim_native_cpu_index();

    if (g_timer_init_result[cpu] != BM_OK) {
        return g_timer_init_result[cpu];
    }
    g_tick_freq[cpu] = freq_hz ? freq_hz : 1000u;
    return BM_OK;
}

static void native_timer_stop(void) {
    g_tick_callback[sim_native_cpu_index()] = NULL;
}

static uint32_t native_timer_get_ticks(void) {
    return g_tick_count[sim_native_cpu_index()];
}

static uint32_t native_timer_get_freq(void) {
    return g_tick_freq[sim_native_cpu_index()];
}

static void native_timer_set_callback(void (*cb)(void)) {
    g_tick_callback[sim_native_cpu_index()] = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    native_timer_init,
    native_timer_stop,
    native_timer_get_ticks,
    native_timer_get_freq,
    native_timer_set_callback,
};

void bm_sim_native_timer_fire_callback(uint32_t cpu) {
    void (*cb)(void) = g_tick_callback[cpu];

    if (!cb) {
        return;
    }
    (void)cpu;
    g_sim_native_isr_depth[cpu]++;
    cb();
    g_sim_native_isr_depth[cpu]--;
}

void bm_hal_timer_native_advance_ticks(uint32_t delta) {
    bm_hal_timer_native_advance_ticks_on_cpu(sim_native_cpu_index(), delta);
}

void bm_hal_timer_native_advance_ticks_on_cpu(uint32_t cpu, uint32_t delta) {
    uint32_t i;

    if (cpu >= BM_CONFIG_CPU_COUNT) {
        return;
    }
    for (i = 0u; i < delta; ++i) {
        g_tick_count[cpu]++;
        bm_sim_native_timer_fire_callback(cpu);
    }
}

void bm_hal_timer_native_jump_ticks(uint32_t delta) {
    uint32_t cpu = sim_native_cpu_index();

    g_tick_count[cpu] += delta;
    bm_sim_native_timer_fire_callback(cpu);
}

void bm_hal_timer_native_reset_ticks(void) {
    g_tick_count[sim_native_cpu_index()] = 0u;
}

void bm_hal_timer_native_deinit(void) {
    uint32_t cpu = sim_native_cpu_index();

    g_tick_freq[cpu] = 0u;
    g_tick_count[cpu] = 0u;
    g_tick_callback[cpu] = NULL;
}

void bm_hal_timer_native_set_init_result(int result) {
    g_timer_init_result[sim_native_cpu_index()] = result;
}

uint32_t bm_hal_timer_native_ticks_on_cpu(uint32_t cpu) {
    return (cpu < BM_CONFIG_CPU_COUNT) ? g_tick_count[cpu] : 0u;
}

uint32_t bm_hal_timer_native_freq_on_cpu(uint32_t cpu) {
    return (cpu < BM_CONFIG_CPU_COUNT) ? g_tick_freq[cpu] : 0u;
}

/* --- uart --- */
static void (*g_uart_rx_cb)(uint8_t c);

static int native_uart_init(void *config) {
    (void)config;
    return BM_OK;
}

static int native_uart_send(const uint8_t *data, size_t len) {
    fwrite(data, 1, len, stdout);
    fflush(stdout);
    return BM_OK;
}

static size_t native_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void native_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    g_uart_rx_cb = cb;
    (void)g_uart_rx_cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    native_uart_init,
    native_uart_send,
    native_uart_recv,
    native_uart_set_rx_callback,
};

/* --- wdg --- */
static uint32_t g_wdg_feed_count;

static int native_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    return BM_OK;
}

static void native_wdg_feed(void) {
    g_wdg_feed_count++;
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    native_wdg_init,
    native_wdg_feed,
};

uint32_t bm_hal_wdg_native_get_feed_count(void) {
    return g_wdg_feed_count;
}

void bm_hal_wdg_native_reset_feed_count(void) {
    g_wdg_feed_count = 0u;
}

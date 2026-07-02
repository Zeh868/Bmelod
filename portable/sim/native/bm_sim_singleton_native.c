/* SPDX-License-Identifier: GPL-3.0-or-later */
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
#include "hal/bm_hal_uptime.h"

#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

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

/* --- uptime 测试辅助偏移量（#9-2a）--- */

/**
 * @brief 测试用 uptime 偏移量（纳秒），由 bm_hal_uptime_native_advance_us 操控
 *
 * 单调递增，setUp 时通过 bm_hal_uptime_native_reset() 归零。
 */
static volatile uint64_t s_uptime_offset_ns;

/**
 * @brief 纯虚拟时钟开关（0=真实时钟+偏移量，非0=只返回偏移量）
 *
 * 由 bm_hal_uptime_native_set_virtual 显式控制；bm_hal_uptime_native_reset()
 * 不改变此开关状态。
 */
static volatile int s_uptime_virtual;

/**
 * @brief 测试辅助：将 uptime 偏移量推进 delta_us 微秒
 *
 * @param delta_us 推进量（微秒）
 */
void bm_hal_uptime_native_advance_us(uint64_t delta_us) {
    s_uptime_offset_ns += delta_us * 1000u;
}

/**
 * @brief 测试辅助：重置 uptime 偏移量为 0
 *
 * 仅清零偏移量，不改变虚拟时钟开关（开关由测试通过
 * bm_hal_uptime_native_set_virtual 显式控制）。
 */
void bm_hal_uptime_native_reset(void) {
    s_uptime_offset_ns = 0u;
}

/**
 * @brief 测试辅助：切换 uptime 是否使用纯虚拟时钟
 *
 * @param enable 非 0 启用纯虚拟时钟；0 恢复真实时钟 + 偏移量
 */
void bm_hal_uptime_native_set_virtual(int enable) {
    s_uptime_virtual = enable;
}

#ifdef _WIN32
/**
 * @brief native_sim Windows 单调时钟后端（QueryPerformanceCounter）
 *
 * 首次调用时读取 QueryPerformanceFrequency 并缓存；此后每次通过
 * QueryPerformanceCounter 得到当前计数，换算为纳秒：
 *   ns = (count - base) * 1e9 / freq
 *
 * 同样拆分避免 count * 1e9 的中间溢出。
 * 测试时可通过 bm_hal_uptime_native_advance_us() 叠加偏移，模拟时间流逝；
 * 若通过 bm_hal_uptime_native_set_virtual(1) 启用纯虚拟时钟，则不叠加
 * QPC 真实分量，只返回偏移量，消除微秒级精确断言的墙钟泄漏。
 *
 * @return 自首次调用起经过的纳秒数（含测试偏移量，uint64_t，单调不减）；
 *         纯虚拟时钟模式下只返回偏移量
 */
uint64_t bm_hal_uptime_ns_raw(void) {
    static LARGE_INTEGER s_freq;
    static LARGE_INTEGER s_base;
    static int s_init;
    LARGE_INTEGER now;
    uint64_t delta;
    uint64_t freq;
    uint64_t real_ns;

    if (s_uptime_virtual) {
        return s_uptime_offset_ns;
    }
    if (!s_init) {
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_base);
        s_init = 1;
    }
    QueryPerformanceCounter(&now);
    delta = (uint64_t)(now.QuadPart - s_base.QuadPart);
    freq  = (uint64_t)s_freq.QuadPart;
    if (freq == 0u) {
        return s_uptime_offset_ns;
    }
    /* 拆分运算避免 delta * 1e9 中间溢出 */
    real_ns = (delta / freq) * 1000000000u
            + (delta % freq) * 1000000000u / freq;
    return real_ns + s_uptime_offset_ns;
}
#else /* POSIX */
/**
 * @brief native_sim POSIX 单调时钟后端（clock_gettime CLOCK_MONOTONIC）
 *
 * 测试时可通过 bm_hal_uptime_native_advance_us() 叠加偏移，模拟时间流逝；
 * 若通过 bm_hal_uptime_native_set_virtual(1) 启用纯虚拟时钟，则不叠加
 * clock_gettime 真实分量，只返回偏移量，消除微秒级精确断言的墙钟泄漏。
 *
 * @return 自系统启动起经过的纳秒数（含测试偏移量，uint64_t，单调不减）；
 *         纯虚拟时钟模式下只返回偏移量
 */
uint64_t bm_hal_uptime_ns_raw(void) {
    struct timespec ts;

    if (s_uptime_virtual) {
        return s_uptime_offset_ns;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u
         + (uint64_t)ts.tv_nsec
         + s_uptime_offset_ns;
}
#endif /* _WIN32 */

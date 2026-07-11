/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_port.c
 * @brief Bmelod Port 组合模板 — arch 层 + vendor 弱钩子
 *
 * 复制到应用工程，对接厂商 HAL。文档：docs/04-移植与IDE集成/03-Port移植层bm_port.md
 *
 * 集成要点：
 * 1. **不要**在本文件定义 `bm_drv_critical_api` / `bm_drv_memory_api` — 由
 *    `portable/arch/<id>/`（`bm_arch_drv_bundle.c`）提供。
 * 2. 将 `portable/arch/<id>` 加入 include path，以便使用 `bm_arch_portmacro.h`。
 * 3. 下方 timer/uart/wdg 为弱符号示例；应用可提供强符号覆盖，或改用 vendor 单例。
 * 4. 使用 `BM_BACKEND` pack 时通常无需本文件（pack 已链 arch + vendor）。
 * 5. 若定时器 ISR（`bm_port_timer_isr`）派发的用户回调可能包含浮点运算
 *    （例如电流环等控制律回调），必须在派发前后调用 `portable/arch/<id>/
 *    bm_arch_isr_fpu.h` 提供的 `bm_arch_isr_fpu_enter/exit` 守卫——多数裸机
 *    ISR 入口默认不保存/不开启浮点协处理器现场，中断内直接跑浮点会踩坑
 *    （轻则现场污染，重则触发协处理器异常复位，详见该头文件各架构平台
 *    真相注释）。见 `bm_port_timer_isr` 内的示范。
 *
 * @author zeh (china_qzh@163.com)
 * @version 2.2
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       2.0            zeh            组合模板：arch 头 + vendor 弱钩子
 * 2026-06-15       2.1            zeh            修正弱符号覆盖点为全局 API 对象
 * 2026-07-11       2.2            zeh            timer_isr 补 ISR FPU 守卫调用示范（bm_arch_isr_fpu.h）
 *
 */
#include <stddef.h>
#include <stdint.h>

#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_types.h"

/*
 * 将 portable/arch/<id> 加入 include path 后取消下行注释，例如 armv7em：
 * #include "bm_arch_portmacro.h"
 */

#if defined(__GNUC__) || defined(__clang__)
#define BM_PORT_WEAK __attribute__((weak))
#else
#define BM_PORT_WEAK
#endif

static void (*g_tick_cb)(void);
static uint32_t g_tick_hz;

static int port_timer_init(uint32_t freq_hz) {
    g_tick_hz = freq_hz;
    return BM_OK;
}

static void port_timer_stop(void) {
    g_tick_cb = NULL;
}

static uint32_t port_timer_get_ticks(void) {
    return 0u;
}

static uint32_t port_timer_get_freq(void) {
    return g_tick_hz;
}

static void port_timer_set_callback(void (*cb)(void)) {
    g_tick_cb = cb;
}

BM_PORT_WEAK const struct bm_timer_driver_api bm_drv_timer_api = {
    port_timer_init,
    port_timer_stop,
    port_timer_get_ticks,
    port_timer_get_freq,
    port_timer_set_callback,
};

/**
 * @brief 应用定时器 ISR 中调用，转发框架 tick 回调。
 *
 * @note 若 g_tick_cb 链路可能触达浮点回调（如控制律），须在派发前后加
 *       ISR FPU 守卫。将 `portable/arch/<id>` 加入 include path 后取消下方
 *       注释即可接线（no-op 架构上零开销，接线不影响行为）：
 * @code
 * #include "<archid>/bm_arch_isr_fpu.h"
 * static uint8_t g_fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));
 * void bm_port_timer_isr(void) {
 *     unsigned prev = bm_arch_isr_fpu_enter(g_fpu_sa);
 *     if (g_tick_cb) {
 *         g_tick_cb();
 *     }
 *     bm_arch_isr_fpu_exit(g_fpu_sa, prev);
 * }
 * @endcode
 */
void bm_port_timer_isr(void) {
    if (g_tick_cb) {
        g_tick_cb();
    }
}

static int port_uart_init(void *config) {
    (void)config;
    return BM_OK;
}

static int port_uart_send(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return BM_OK;
}

static size_t port_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void port_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

BM_PORT_WEAK const struct bm_uart_driver_api bm_drv_uart_api = {
    port_uart_init,
    port_uart_send,
    port_uart_recv,
    port_uart_set_rx_callback,
};

static int port_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    return BM_OK;
}

static void port_wdg_feed(void) {
}

BM_PORT_WEAK const struct bm_wdg_driver_api bm_drv_wdg_api = {
    port_wdg_init,
    port_wdg_feed,
};

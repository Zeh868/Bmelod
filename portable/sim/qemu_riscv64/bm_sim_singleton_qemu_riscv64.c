/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_riscv64.c
 * @brief QEMU RISC-V64 virt 仿真单例驱动（定时器 / UART / 看门狗桩）
 * @maturity E1
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
 * 2026-08-01       1.0            Codex          补全中文 Doxygen 合规注释
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_hal_uart.h"
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

/**
 * @brief 初始化 RISC-V64 仿真定时器。
 * @param freq_hz 定时器频率，单位为 Hz；传入 0 时使用 1000 Hz。
 * @return 成功返回 BM_OK。
 */
static int rv64_timer_init(uint32_t freq_hz) {
    g_tick_freq = (freq_hz > 0u) ? freq_hz : 1000u;
    g_ticks = 0u;
    BM_LOGI(TAG_TIMER, "init: freq_hz=%u (stub)", g_tick_freq);
    return BM_OK;
}

/**
 * @brief 停止 RISC-V64 仿真定时器并解除 tick 回调。
 */
static void rv64_timer_stop(void) {
    g_tick_cb = NULL;
}

/**
 * @brief 读取 RISC-V64 仿真定时器的当前 tick 计数。
 * @return 当前 tick 计数。
 */
static uint32_t rv64_timer_get_ticks(void) {
    return g_ticks;
}

/**
 * @brief 读取 RISC-V64 仿真定时器频率。
 * @return 定时器频率，单位为 Hz。
 */
static uint32_t rv64_timer_get_freq(void) {
    return g_tick_freq;
}

/**
 * @brief 设置 RISC-V64 仿真定时器的 tick 回调。
 * @param cb tick 回调函数；传入 NULL 时解除绑定。
 */
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

/**
 * @brief 初始化 RISC-V64 QEMU UART 仿真桩。
 * @param dev UART 设备实例；当前仿真桩不使用该参数。
 * @param config UART 配置；当前仿真桩不使用该参数。
 * @return 成功返回 BM_OK。
 */
static int rv64_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev;
    (void)config;
    BM_LOGI(TAG_UART, "init: virt uart stub");
    return BM_OK;
}

/**
 * @brief 通过 RISC-V64 QEMU UART 仿真桩发送数据。
 * @param dev UART 设备实例；当前仿真桩不使用该参数。
 * @param data 待发送数据缓冲区；当前仿真桩不读取该缓冲区。
 * @param len 待发送数据长度，单位为字节。
 * @return 成功返回 BM_OK。
 */
static int rv64_uart_send(const struct bm_hal_uart *dev,
                            const uint8_t *data, size_t len) {
    (void)dev;
    (void)data;
    (void)len;
    return BM_OK;
}

/**
 * @brief 从 RISC-V64 QEMU UART 仿真桩接收数据。
 * @param dev UART 设备实例；当前仿真桩不使用该参数。
 * @param data 接收缓冲区；当前仿真桩不写入该缓冲区。
 * @param max_len 接收缓冲区容量，单位为字节。
 * @return 当前仿真桩固定返回 0，表示未接收到数据。
 */
static size_t rv64_uart_recv(const struct bm_hal_uart *dev,
                               uint8_t *data, size_t max_len) {
    (void)dev;
    (void)data;
    (void)max_len;
    return 0u;
}

/**
 * @brief 设置 RISC-V64 QEMU UART 仿真桩的接收回调。
 * @param dev UART 设备实例；当前仿真桩不使用该参数。
 * @param cb 单字节接收回调；当前仿真桩忽略该参数且不会触发回调。
 */
static void rv64_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                        void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
}

static const struct bm_uart_driver_api g_uart_api = {
    rv64_uart_init,
    rv64_uart_send,
    rv64_uart_recv,
    rv64_uart_set_rx_callback,
};

/** @brief 默认控制台 UART 设备（统一实例模型，见 bm_hal_uart.h）。 */
const bm_hal_uart_t bm_uart_default = { &g_uart_api, NULL };

/**
 * @brief 初始化 RISC-V64 QEMU 看门狗仿真桩。
 * @param timeout_ms 看门狗超时时间，单位为毫秒；当前仿真桩不使用该参数。
 * @return 成功返回 BM_OK。
 */
static int rv64_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

/**
 * @brief 喂养 RISC-V64 QEMU 看门狗仿真桩。
 */
static void rv64_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    rv64_wdg_init,
    rv64_wdg_feed,
};

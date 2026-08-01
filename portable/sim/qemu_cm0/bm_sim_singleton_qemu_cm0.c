/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_cm0.c
 * @brief QEMU Cortex-M0 仿真单例驱动（定时器 / UART / 看门狗 / 单调时钟）
 * @maturity E1
 *
 * 临界区与内存屏障由 `bm_port_arch_armv6m` 提供。
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            从 qemu_cortex_m0 singleton 拆分
 * 2026-06-26       1.1            zeh            添加 bm_hal_uptime_ns_raw()（路线图 #9 时间基统一 1a）
 * 2026-07-11       1.2            zeh            tick 回调派发接入 arch 层 FPU 守卫（bm_arch_isr_fpu.h，armv6m 无 FPU 恒 no-op）
 * 2026-07-16       1.3            zeh            semihosting 写改用 SYS_WRITE(0x05) 参数块按 len 写入（原 SYS_WRITE0 忽略 len、按 NUL 结尾整段写）
 *
 * 2026-08-01       1.3            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_hal_uart.h"
#include "bm_drv_wdg.h"
#include "bm_log.h"
#include "bm_types.h"
#include "hal/bm_hal_uptime.h"
#include "armv6m/bm_arch_isr_fpu.h"

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

/**
 * @brief 根据当前频率重新编程 QEMU SysTick 周期。
 */
static void qemu_timer_program_period(void) {
    uint32_t cc = NRF_TIMER_CLK_HZ / g_qemu_tick_freq;
    if (cc == 0u) {
        cc = 1u;
    }
    TIMER1_CC0 = cc;
}

/** @brief tick ISR 内 FPU 现场保存区（armv6m 无 FPU 硬件，恒 no-op，接线预留）。 */
static uint8_t g_tick_cp0_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));

/**
 * @brief TIMER1 比较中断服务函数。
 *
 * g_qemu_tick_cb 派发经 bm_arch_isr_fpu_enter/exit
 * （portable/arch/armv6m/bm_arch_isr_fpu.h）包裹；Cortex-M0 无 FPU 硬件恒
 * no-op，此处接线只为统一调用点，行为不变。
 */
void TIMER1_IRQHandler(void) {
    unsigned cp_prev;

    if (TIMER1_EVENTS_COMPARE0 == 0U) {
        return;
    }
    TIMER1_EVENTS_COMPARE0 = 0U;
    TIMER1_TASKS_CLEAR = 1U;
    g_qemu_ticks++;
    cp_prev = bm_arch_isr_fpu_enter(g_tick_cp0_sa);
    if (g_qemu_tick_cb) {
        g_qemu_tick_cb();
    }
    bm_arch_isr_fpu_exit(g_tick_cp0_sa, cp_prev);
    TIMER1_TASKS_START = 1U;
}

/**
 * @brief 初始化当前逻辑 CPU 的定时器。
 * @param freq_hz 定时器频率，单位为 Hz；传入 0 时使用该端口默认频率。
 * @return 成功返回 BM_OK。
 */
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

/**
 * @brief 停止定时器设备。
 */
static void qemu_timer_stop(void) {
    TIMER1_TASKS_STOP = 1U;
    TIMER1_INTENCLR = (1U << 16);
    g_qemu_tick_cb = NULL;
    BM_LOGI(TAG_TIMER, "stop");
}

/**
 * @brief 读取当前逻辑 CPU 的定时器 tick 计数。
 * @return 当前逻辑 CPU 的定时器 tick 计数。
 */
static uint32_t qemu_timer_get_ticks(void) {
    return g_qemu_ticks;
}

/**
 * @brief 读取当前定时器频率。
 * @return 定时器频率，单位为 Hz；设备无效时返回 0。
 */
static uint32_t qemu_timer_get_freq(void) {
    return g_qemu_tick_freq;
}

/**
 * @brief 设置定时器回调。
 * @param cb tick 回调；传入 NULL 时解除绑定。
 */
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

/* SYS_WRITE(0x05)：r1 指向参数块 [fd=1(主机 stdout), buf, len]，按 len 精确写入 */
static void qemu_semihosting_write(const uint8_t *data, size_t len) {
    uint32_t param[3] = {1u, (uint32_t)(uintptr_t)data, (uint32_t)len};
    __asm volatile (
        "movs r0, #0x05\n"
        "movs r1, %0\n"
        "bkpt 0xAB\n"
        :
        : "r"(param)
        : "r0", "r1", "memory"
    );
}

/**
 * @brief 初始化UART端口。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param config 设备初始化配置；当前实现不使用该参数。
 * @return 成功返回 BM_OK。
 */
static int qemu_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev;
    (void)config;
    BM_LOGI(TAG_UART, "init: semihosting backend");
    return BM_OK;
}

/**
 * @brief 通过UART发送数据。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param data 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK。
 */
static int qemu_uart_send(const struct bm_hal_uart *dev,
                            const uint8_t *data, size_t len) {
    (void)dev;
    qemu_semihosting_write(data, len);
    return BM_OK;
}

/**
 * @brief 从UART接收数据。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param data 接收数据缓冲区；当前仿真桩不读写该缓冲区。
 * @param max_len 接收缓冲区容量，单位为字节。
 * @return 实际写入接收缓冲区的字节数；无数据或参数无效时返回 0。
 */
static size_t qemu_uart_recv(const struct bm_hal_uart *dev,
                               uint8_t *data, size_t max_len) {
    (void)dev;
    (void)data;
    (void)max_len;
    return 0u;
}

/**
 * @brief 设置UART接收回调。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param cb 接收回调；当前仿真桩忽略该参数且不会触发回调。
 */
static void qemu_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                        void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
}

static const struct bm_uart_driver_api g_uart_api = {
    qemu_uart_init,
    qemu_uart_send,
    qemu_uart_recv,
    qemu_uart_set_rx_callback,
};

/** @brief 默认控制台 UART 设备（统一实例模型，见 bm_hal_uart.h）。 */
const bm_hal_uart_t bm_uart_default = { &g_uart_api, NULL };

/**
 * @brief 初始化看门狗仿真桩。
 * @param timeout_ms 看门狗超时时间，单位为毫秒；当前仿真桩不使用该值。
 * @return 成功返回 BM_OK。
 */
static int qemu_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: timeout_ms=%u (stub)", timeout_ms);
    return BM_OK;
}

/**
 * @brief 喂养看门狗仿真桩。
 */
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

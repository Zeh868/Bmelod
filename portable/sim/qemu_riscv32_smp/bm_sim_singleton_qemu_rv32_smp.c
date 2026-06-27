/**
 * @file bm_sim_singleton_qemu_rv32_smp.c
 * @brief QEMU RISC-V32 virt SMP 单例驱动（CLINT 定时器 / ns16550a UART / 看门狗桩）
 *
 * 临界区与内存屏障由 `bm_port_arch_riscv32` 提供；M-mode trap 由 boot 层 `_trap_entry` 分发。
 * CLINT mtime/mtimecmp 为 64 位，RV32 上通过两次 32 位访问读写。
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

/** QEMU virt CLINT 基址 */
#define CLINT_BASE         0x02000000UL
/** CLINT 软件中断寄存器 */
#define CLINT_MSIP(h)      (*(volatile uint32_t *)(CLINT_BASE + 4u * (h)))

/** QEMU virt 默认 mtime 频率（10 MHz） */
#define RV32_SMP_MTIME_HZ  10000000u

/** ns16550a UART 基址 */
#define UART_BASE          0x10000000UL
#define UART_THR           (*(volatile uint8_t *)(UART_BASE + 0u))
#define UART_LSR           (*(volatile uint8_t *)(UART_BASE + 5u))
/** LSR THRE：发送保持寄存器空 */
#define UART_LSR_THRE      (1u << 5)

static uint32_t g_tick_freq[BM_CONFIG_CPU_COUNT];
static volatile uint32_t g_ticks[BM_CONFIG_CPU_COUNT];
static void (*g_tick_cb[BM_CONFIG_CPU_COUNT])(void);
static uint64_t g_timer_interval[BM_CONFIG_CPU_COUNT];
static int g_timer_armed[BM_CONFIG_CPU_COUNT];

/**
 * @brief 读取 CLINT mtime（64 位，RV32 分两次读并处理高位回绕）
 */
static uint64_t clint_get_mtime(void) {
    volatile uint32_t *mtime = (volatile uint32_t *)(CLINT_BASE + 0xBFF8u);
    uint32_t lo;
    uint32_t hi;

    do {
        hi = mtime[1];
        lo = mtime[0];
    } while (mtime[1] != hi);
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/**
 * @brief 写入 CLINT mtimecmp（64 位，RV32 分两次写防止中间态触发）
 */
static void clint_set_mtimecmp(uint32_t hart, uint64_t val) {
    volatile uint32_t *lo = (volatile uint32_t *)(CLINT_BASE + 0x4000u + 8u * hart);
    volatile uint32_t *hi = lo + 1;

    *hi = 0xFFFFFFFFu;
    *lo = (uint32_t)val;
    *hi = (uint32_t)(val >> 32);
}

/**
 * @brief 返回当前 hart 索引（越界时回落 0）
 */
static uint32_t rv32_smp_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

/**
 * @brief 为指定 hart 装载下一次 mtimecmp 并开 MIE
 */
static void rv32_smp_timer_arm(uint32_t cpu) {
    uint64_t now = clint_get_mtime();

    clint_set_mtimecmp(cpu, now + g_timer_interval[cpu]);
    g_timer_armed[cpu] = 1;
    __asm volatile ("csrsi mstatus, 8" ::: "memory");
}

static int rv32_smp_timer_init(uint32_t freq_hz) {
    uint32_t cpu = rv32_smp_cpu_index();
    uint32_t hz = (freq_hz > 0u) ? freq_hz : 1000u;

    g_tick_freq[cpu] = hz;
    g_ticks[cpu] = 0u;
    g_timer_interval[cpu] =
        (uint64_t)RV32_SMP_MTIME_HZ / (uint64_t)hz;
    if (g_timer_interval[cpu] == 0u) {
        g_timer_interval[cpu] = 1u;
    }
    rv32_smp_timer_arm(cpu);
    BM_LOGI(TAG_TIMER, "init: cpu=%u freq_hz=%u", (unsigned)cpu, hz);
    return BM_OK;
}

static void rv32_smp_timer_stop(void) {
    uint32_t cpu = rv32_smp_cpu_index();

    g_tick_cb[cpu] = NULL;
    g_timer_armed[cpu] = 0;
}

static uint32_t rv32_smp_timer_get_ticks(void) {
    return g_ticks[rv32_smp_cpu_index()];
}

static uint32_t rv32_smp_timer_get_freq(void) {
    return g_tick_freq[rv32_smp_cpu_index()];
}

static void rv32_smp_timer_set_callback(void (*cb)(void)) {
    g_tick_cb[rv32_smp_cpu_index()] = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    rv32_smp_timer_init,
    rv32_smp_timer_stop,
    rv32_smp_timer_get_ticks,
    rv32_smp_timer_get_freq,
    rv32_smp_timer_set_callback,
};

/**
 * @brief M-mode 机器定时器中断（由 startup trap 向量调用）
 */
void qemu_rv32_smp_on_timer_irq(void) {
    uint32_t cpu = rv32_smp_cpu_index();
    void (*cb)(void);

    if (!g_timer_armed[cpu]) {
        return;
    }
    clint_set_mtimecmp(cpu, clint_get_mtime() + g_timer_interval[cpu]);
    g_ticks[cpu]++;
    cb = g_tick_cb[cpu];
    if (cb) {
        cb();
    }
}

/**
 * @brief M-mode 软件中断（IPI 唤醒从核后清 MSIP）
 */
void qemu_rv32_smp_on_software_irq(void) {
    uint32_t cpu = rv32_smp_cpu_index();

    CLINT_MSIP(cpu) = 0u;
}

static int rv32_smp_uart_init(void *config) {
    (void)config;
    BM_LOGI(TAG_UART, "init: ns16550a");
    return BM_OK;
}

static int rv32_smp_uart_send(const uint8_t *data, size_t len) {
    size_t i;

    if (!data || len == 0u) {
        return BM_OK;
    }
    for (i = 0u; i < len; i++) {
        while ((UART_LSR & UART_LSR_THRE) == 0u) {
        }
        UART_THR = data[i];
    }
    return BM_OK;
}

static size_t rv32_smp_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void rv32_smp_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    rv32_smp_uart_init,
    rv32_smp_uart_send,
    rv32_smp_uart_recv,
    rv32_smp_uart_set_rx_callback,
};

static int rv32_smp_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

static void rv32_smp_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    rv32_smp_wdg_init,
    rv32_smp_wdg_feed,
};

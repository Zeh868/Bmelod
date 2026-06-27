/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_aarch64_smp.c
 * @brief QEMU AArch64 virt SMP 仿真单例驱动（Generic Timer / PL011 / GICv2）
 *
 * 临界区与内存屏障由 `bm_port_arch_aarch64` 提供。
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
#include "hal/bm_hal_uptime.h"

#include <stddef.h>
#include <stdint.h>

#define TAG_TIMER "hal_timer"
#define TAG_UART  "hal_uart"
#define TAG_WDG   "hal_wdg"

/** GICv2（QEMU virt 默认） */
#define GICD_BASE           0x08000000UL
#define GICC_BASE           0x08010000UL

#define GICD_CTLR           (*(volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_TYPER          (*(volatile uint32_t *)(GICD_BASE + 0x004))
#define GICD_ISENABLER(n)   (*(volatile uint32_t *)(GICD_BASE + 0x100 + (n) * 4u))
#define GICD_ICENABLER(n)   (*(volatile uint32_t *)(GICD_BASE + 0x180 + (n) * 4u))
#define GICD_IPRIORITYR(n)  (*(volatile uint8_t *)(GICD_BASE + 0x400 + (n)))
#define GICD_ITARGETSR(n)   (*(volatile uint8_t *)(GICD_BASE + 0x800 + (n)))
#define GICD_ICFGR(n)       (*(volatile uint32_t *)(GICD_BASE + 0xC00 + (n) * 4u))

#define GICC_CTLR           (*(volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR            (*(volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR            (*(volatile uint32_t *)(GICC_BASE + 0x00C))
#define GICC_EOIR           (*(volatile uint32_t *)(GICC_BASE + 0x010))

/** PL011 UART */
#define UART_BASE           0x09000000UL
#define UART_DR             (*(volatile uint32_t *)(UART_BASE + 0x000))
#define UART_FR             (*(volatile uint32_t *)(UART_BASE + 0x018))
#define UART_FR_TXFF        (1u << 5)

/** ARM Generic Timer PPI */
#define BM_AARCH64_TIMER_IRQ_ID  30u

static uint32_t g_timer_freq_hz;
static uint32_t g_tick_freq_hz;
static volatile uint32_t g_ticks;
static void (*g_tick_cb)(void);
static uint64_t g_timer_cntfrq;
static int g_gic_ready;

/**
 * @brief 读 CNTPCT_EL0 物理计数
 */
static uint64_t bm_aarch64_read_cntpct(void) {
    uint64_t count;

    __asm volatile("mrs %0, cntpct_el0" : "=r"(count));
    return count;
}

/**
 * @brief 读 CNTFRQ_EL0
 */
static uint64_t bm_aarch64_read_cntfrq(void) {
    uint64_t freq;

    __asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

/**
 * @brief 初始化 GIC 分发器（仅 bootstrap 调用一次）
 */
static void bm_aarch64_gic_dist_init(void) {
    uint32_t irq;

    GICD_CTLR = 0u;
    for (irq = 32u; irq < 1020u; irq += 32u) {
        GICD_ICENABLER(irq / 32u) = 0xFFFFFFFFu;
    }
    for (irq = 0u; irq < 1020u; irq++) {
        GICD_IPRIORITYR(irq) = 0xA0u;
        GICD_ITARGETSR(irq) = 0x01u;
    }
    for (irq = 32u; irq < 1020u; irq += 16u) {
        GICD_ICFGR(irq / 16u) = 0u;
    }
    GICD_ISENABLER(BM_AARCH64_TIMER_IRQ_ID / 32u) =
        (1u << (BM_AARCH64_TIMER_IRQ_ID % 32u));
    GICD_CTLR = 1u;
}

/**
 * @brief 初始化当前 CPU 的 GIC CPU 接口
 */
static void bm_aarch64_gic_cpu_init(void) {
    GICC_PMR = 0xFFu;
    GICC_CTLR = 1u;
}

/**
 * @brief 按 tick 频率重装 Generic Timer 比较值
 */
static void bm_aarch64_timer_rearm(void) {
    uint64_t now;
    uint64_t delta;
    uint64_t compare;

    if (g_tick_freq_hz == 0u || g_timer_cntfrq == 0u) {
        return;
    }
    now = bm_aarch64_read_cntpct();
    delta = g_timer_cntfrq / (uint64_t)g_tick_freq_hz;
    if (delta == 0u) {
        delta = 1u;
    }
    compare = now + delta;
    __asm volatile("msr cntp_cval_el0, %0" ::"r"(compare));
    __asm volatile("msr cntp_ctl_el0, %0" ::"r"(1ULL));
}

static int aarch64_timer_init(uint32_t freq_hz) {
    g_tick_freq_hz = (freq_hz > 0u) ? freq_hz : 1000u;
    g_timer_cntfrq = bm_aarch64_read_cntfrq();
    if (g_timer_cntfrq == 0u) {
        g_timer_cntfrq = 62500000u;
    }
    g_timer_freq_hz = (uint32_t)g_timer_cntfrq;
    g_ticks = 0u;

    if (bm_hal_cpu_is_bootstrap() && !g_gic_ready) {
        bm_aarch64_gic_dist_init();
        g_gic_ready = 1;
    }
    bm_aarch64_gic_cpu_init();
    bm_aarch64_timer_rearm();
    BM_LOGI(TAG_TIMER, "init: tick_hz=%u cntfrq=%u",
            (unsigned)g_tick_freq_hz, (unsigned)g_timer_freq_hz);
    return BM_OK;
}

static void aarch64_timer_stop(void) {
    g_tick_cb = NULL;
    __asm volatile("msr cntp_ctl_el0, %0" ::"r"(0ULL));
}

static uint32_t aarch64_timer_get_ticks(void) {
    return g_ticks;
}

static uint32_t aarch64_timer_get_freq(void) {
    return g_tick_freq_hz;
}

static void aarch64_timer_set_callback(void (*cb)(void)) {
    g_tick_cb = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    aarch64_timer_init,
    aarch64_timer_stop,
    aarch64_timer_get_ticks,
    aarch64_timer_get_freq,
    aarch64_timer_set_callback,
};

/**
 * @brief IRQ 顶层分发（由异常向量汇编调用）
 */
void bm_qemu_aarch64_irq_dispatch(void) {
    uint32_t iar = GICC_IAR;
    uint32_t irq_id = iar & 0x3FFu;

    if (irq_id == BM_AARCH64_TIMER_IRQ_ID) {
        g_ticks++;
        if (g_tick_cb) {
            g_tick_cb();
        }
        bm_aarch64_timer_rearm();
    }
    GICC_EOIR = iar;
}

static int aarch64_uart_init(void *config) {
    (void)config;
    BM_LOGI(TAG_UART, "init: PL011 @0x%08X", (unsigned)UART_BASE);
    return BM_OK;
}

static int aarch64_uart_send(const uint8_t *data, size_t len) {
    size_t i;

    if (!data && len > 0u) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < len; i++) {
        while ((UART_FR & UART_FR_TXFF) != 0u) {
            __asm volatile("yield");
        }
        UART_DR = (uint32_t)data[i];
    }
    return BM_OK;
}

static size_t aarch64_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void aarch64_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    aarch64_uart_init,
    aarch64_uart_send,
    aarch64_uart_recv,
    aarch64_uart_set_rx_callback,
};

static int aarch64_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

static void aarch64_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    aarch64_wdg_init,
    aarch64_wdg_feed,
};

/**
 * @brief AArch64 Generic Timer 单调时钟后端（CNTPCT_EL0 + CNTFRQ_EL0）
 *
 * 将 64 位物理计数换算为纳秒：
 *   ns = (count / freq) * 1e9 + (count % freq) * 1e9 / freq
 *
 * 拆分运算避免 count * 1e9 的中间溢出（count 最大约 2^64/62.5e6 秒量级）。
 * CNTFRQ 首次读取后缓存（硬件初始化后不变）。
 *
 * @return 自系统启动起经过的纳秒数（uint64_t，单调不减）
 */
uint64_t bm_hal_uptime_ns_raw(void) {
    static uint64_t s_cntfrq;
    uint64_t count;
    uint64_t freq;
    uint64_t ns;

    if (s_cntfrq == 0u) {
        s_cntfrq = bm_aarch64_read_cntfrq();
        if (s_cntfrq == 0u) {
            s_cntfrq = 62500000u; /* 62.5 MHz 默认值（QEMU virt） */
        }
    }
    count = bm_aarch64_read_cntpct();
    freq  = s_cntfrq;
    /* 整秒部分 + 余数部分，避免 count * 1e9 溢出 */
    ns = (count / freq) * 1000000000u
       + (count % freq) * 1000000000u / freq;
    return ns;
}

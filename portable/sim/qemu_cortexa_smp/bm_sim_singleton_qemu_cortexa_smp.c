/**
 * @file bm_sim_singleton_qemu_cortexa_smp.c
 * @brief QEMU ARMv7-A virt SMP 仿真单例驱动（Generic Timer / PL011 / GICv2）
 *
 * 临界区与内存屏障由 `bm_port_arch_armv7a` 提供。
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

/** GICv2（QEMU virt ARM32 默认） */
#define GICD_BASE           0x08000000UL
#define GICC_BASE           0x08010000UL

#define GICD_CTLR           (*(volatile uint32_t *)(GICD_BASE + 0x000))
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
#define BM_CORTEXA_TIMER_IRQ_ID  30u

static uint32_t g_tick_freq_hz[BM_CONFIG_CPU_COUNT];
static volatile uint32_t g_ticks[BM_CONFIG_CPU_COUNT];
static void (*g_tick_cb[BM_CONFIG_CPU_COUNT])(void);
static uint32_t g_timer_cntfrq;
static uint64_t g_timer_interval[BM_CONFIG_CPU_COUNT];
static int g_gic_ready;

/**
 * @brief 返回当前 CPU 索引（越界时回落 0）
 */
static uint32_t cortexa_smp_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

/**
 * @brief 读 CNTFRQ（CP15）
 */
static uint32_t bm_cortexa_read_cntfrq(void) {
    uint32_t freq;

    __asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(freq));
    return freq;
}

/**
 * @brief 读 CNTPCT（CP15）
 */
static uint64_t bm_cortexa_read_cntpct(void) {
    uint32_t lo;
    uint32_t hi;

    __asm volatile("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/**
 * @brief 写 CNTP_CVAL（CP15）
 */
static void bm_cortexa_write_cntp_cval(uint64_t compare) {
    uint32_t lo = (uint32_t)compare;
    uint32_t hi = (uint32_t)(compare >> 32);

    __asm volatile("mcrr p15, 2, %0, %1, c14" ::"r"(lo), "r"(hi));
}

/**
 * @brief 写 CNTP_CTL（CP15）
 */
static void bm_cortexa_write_cntp_ctl(uint32_t ctl) {
    __asm volatile("mcr p15, 0, %0, c14, c2, 1" ::"r"(ctl));
}

/**
 * @brief 初始化 GIC 分发器（仅 bootstrap 调用一次）
 */
static void bm_cortexa_gic_dist_init(void) {
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
    GICD_ISENABLER(BM_CORTEXA_TIMER_IRQ_ID / 32u) =
        (1u << (BM_CORTEXA_TIMER_IRQ_ID % 32u));
    GICD_CTLR = 1u;
}

/**
 * @brief 初始化当前 CPU 的 GIC CPU 接口
 */
static void bm_cortexa_gic_cpu_init(void) {
    GICC_PMR = 0xFFu;
    GICC_CTLR = 1u;
}

/**
 * @brief 按 tick 频率重装 Generic Timer 比较值
 */
static void bm_cortexa_timer_rearm(uint32_t cpu) {
    uint64_t now;
    uint64_t compare;

    if (g_tick_freq_hz[cpu] == 0u || g_timer_cntfrq == 0u) {
        return;
    }
    now = bm_cortexa_read_cntpct();
    compare = now + g_timer_interval[cpu];
    bm_cortexa_write_cntp_cval(compare);
    bm_cortexa_write_cntp_ctl(1u);
}

static int cortexa_timer_init(uint32_t freq_hz) {
    uint32_t cpu = cortexa_smp_cpu_index();
    uint32_t hz = (freq_hz > 0u) ? freq_hz : 1000u;

    g_tick_freq_hz[cpu] = hz;
    g_ticks[cpu] = 0u;
    if (g_timer_cntfrq == 0u) {
        g_timer_cntfrq = bm_cortexa_read_cntfrq();
        if (g_timer_cntfrq == 0u) {
            g_timer_cntfrq = 62500000u;
        }
    }
    g_timer_interval[cpu] =
        (uint64_t)g_timer_cntfrq / (uint64_t)hz;
    if (g_timer_interval[cpu] == 0u) {
        g_timer_interval[cpu] = 1u;
    }

    if (bm_hal_cpu_is_bootstrap() && !g_gic_ready) {
        bm_cortexa_gic_dist_init();
        g_gic_ready = 1;
    }
    bm_cortexa_gic_cpu_init();
    bm_cortexa_timer_rearm(cpu);
    BM_LOGI(TAG_TIMER, "init: cpu=%u tick_hz=%u cntfrq=%u",
            (unsigned)cpu, (unsigned)hz, (unsigned)g_timer_cntfrq);
    return BM_OK;
}

static void cortexa_timer_stop(void) {
    uint32_t cpu = cortexa_smp_cpu_index();

    g_tick_cb[cpu] = NULL;
    bm_cortexa_write_cntp_ctl(0u);
}

static uint32_t cortexa_timer_get_ticks(void) {
    return g_ticks[cortexa_smp_cpu_index()];
}

static uint32_t cortexa_timer_get_freq(void) {
    return g_tick_freq_hz[cortexa_smp_cpu_index()];
}

static void cortexa_timer_set_callback(void (*cb)(void)) {
    g_tick_cb[cortexa_smp_cpu_index()] = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    cortexa_timer_init,
    cortexa_timer_stop,
    cortexa_timer_get_ticks,
    cortexa_timer_get_freq,
    cortexa_timer_set_callback,
};

/**
 * @brief IRQ 顶层分发（由异常向量汇编调用）
 */
void bm_qemu_cortexa_irq_dispatch(void) {
    uint32_t cpu = cortexa_smp_cpu_index();
    uint32_t iar = GICC_IAR;
    uint32_t irq_id = iar & 0x3FFu;
    void (*cb)(void);

    if (irq_id == BM_CORTEXA_TIMER_IRQ_ID) {
        g_ticks[cpu]++;
        cb = g_tick_cb[cpu];
        if (cb) {
            cb();
        }
        bm_cortexa_timer_rearm(cpu);
    }
    GICC_EOIR = iar;
}

static int cortexa_uart_init(void *config) {
    (void)config;
    BM_LOGI(TAG_UART, "init: PL011 @0x%08X", (unsigned)UART_BASE);
    return BM_OK;
}

static int cortexa_uart_send(const uint8_t *data, size_t len) {
    size_t i;

    if (!data && len > 0u) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < len; i++) {
        while ((UART_FR & UART_FR_TXFF) != 0u) {
            __asm volatile("wfe");
        }
        UART_DR = (uint32_t)data[i];
    }
    return BM_OK;
}

static size_t cortexa_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void cortexa_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    (void)cb;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    cortexa_uart_init,
    cortexa_uart_send,
    cortexa_uart_recv,
    cortexa_uart_set_rx_callback,
};

static int cortexa_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

static void cortexa_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    cortexa_wdg_init,
    cortexa_wdg_feed,
};

/**
 * @brief ARMv7-A Generic Timer 单调时钟后端（CNTPCT + CNTFRQ）
 *
 * 将 64 位物理计数 CNTPCT 换算为纳秒：
 *   ns = count * 1_000_000_000 / cntfrq
 *
 * 为避免 64 位中间乘积溢出，将 count 拆成高/低两段分别换算后合并：
 *   ns = (count / cntfrq) * 1e9 + (count % cntfrq) * 1e9 / cntfrq
 *
 * CNTFRQ 首次读取后缓存（单调，硬件初始化后不变）。
 *
 * @return 自系统启动起经过的纳秒数（uint64_t，单调不减）
 */
uint64_t bm_hal_uptime_ns_raw(void) {
    static uint32_t s_cntfrq;
    uint64_t count;
    uint64_t freq;
    uint64_t ns;

    if (s_cntfrq == 0u) {
        s_cntfrq = bm_cortexa_read_cntfrq();
        if (s_cntfrq == 0u) {
            s_cntfrq = 62500000u; /* 62.5 MHz 默认值（QEMU virt） */
        }
    }
    count = bm_cortexa_read_cntpct();
    freq  = (uint64_t)s_cntfrq;
    /* 整秒部分 + 余数部分，避免 count * 1e9 溢出 */
    ns = (count / freq) * 1000000000u
       + (count % freq) * 1000000000u / freq;
    return ns;
}

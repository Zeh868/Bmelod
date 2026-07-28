/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_aarch64_smp.c
 * @brief QEMU AArch64 virt SMP 仿真单例驱动（Generic Timer / PL011 / GICv2）
 *
 * 临界区与内存屏障由 `bm_port_arch_aarch64` 提供。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.5
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-11       1.1            zeh            tick 回调派发接入 arch 层 FPU 守卫（bm_arch_isr_fpu.h，aarch64 路径当前仍为 no-op）
 * 2026-07-15       1.2            zeh            GICD_ISENABLER0（IRQ 0-31 为 per-core banked）从 gic_dist_init 移入 gic_cpu_init，从核各自使能定时器 PPI
 * 2026-07-16       1.3            zeh            g_ticks/g_tick_cb/g_tick_freq_hz 改 per-CPU 数组（原共享标量双核互覆、tick 双倍计数），对齐 cortexa SMP 实现；配套 IRQ 向量补存 x8-x18 与 q0-q31/FPCR/FPSR 现场、启动使能 CPACR_EL1.FPEN 并 daifclr 开 IRQ（复位 DAIF 全屏蔽曾致 tick 永不触发）
 * 2026-07-16       1.4            zeh            IRQ 分发跳过 GICv2 保留/伪 IRQ ID（1022/1023），避免无条件写 GICC_EOIR
 * 2026-07-28       1.5            zeh            PL011 发送轮询加入命名上限，超时返回 BM_ERR_TIMEOUT
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_hal_uart.h"
#include "bm_drv_wdg.h"
#include "bm_log.h"
#include "bm_types.h"
#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_uptime.h"
#include "aarch64/bm_arch_isr_fpu.h"

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
/** @brief PL011 发送 FIFO 满轮询上限，超出时返回 BM_ERR_TIMEOUT。 */
#define BM_QEMU_AARCH64_UART_TX_POLL_LIMIT  100000u

/** ARM Generic Timer PPI */
#define BM_AARCH64_TIMER_IRQ_ID  30u

static uint32_t g_timer_freq_hz;
static uint32_t g_tick_freq_hz[BM_CONFIG_CPU_COUNT];
static volatile uint32_t g_ticks[BM_CONFIG_CPU_COUNT];
static void (*g_tick_cb[BM_CONFIG_CPU_COUNT])(void);
static uint64_t g_timer_cntfrq;
static int g_gic_ready;

/**
 * @brief 返回当前 CPU 索引（越界时回落 0，对齐 cortexa SMP 实现）
 */
static uint32_t aarch64_smp_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

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
    GICD_CTLR = 1u;
}

/**
 * @brief 初始化当前 CPU 的 GIC CPU 接口
 *
 * IRQ 0–31（SGI/PPI）的 ISENABLER0 是 per-core banked 寄存器：
 * 每核须写自己那份 banked 位才能使能本核的 Generic Timer PPI，
 * 只在 bootstrap 的 dist_init 里写一次会导致从核收不到 tick。
 */
static void bm_aarch64_gic_cpu_init(void) {
    GICC_PMR = 0xFFu;
    GICC_CTLR = 1u;
    GICD_ISENABLER(BM_AARCH64_TIMER_IRQ_ID / 32u) =
        (1u << (BM_AARCH64_TIMER_IRQ_ID % 32u));
}

/**
 * @brief 按 tick 频率重装 Generic Timer 比较值（per-CPU）
 */
static void bm_aarch64_timer_rearm(uint32_t cpu) {
    uint64_t now;
    uint64_t delta;
    uint64_t compare;

    if (g_tick_freq_hz[cpu] == 0u || g_timer_cntfrq == 0u) {
        return;
    }
    now = bm_aarch64_read_cntpct();
    delta = g_timer_cntfrq / (uint64_t)g_tick_freq_hz[cpu];
    if (delta == 0u) {
        delta = 1u;
    }
    compare = now + delta;
    __asm volatile("msr cntp_cval_el0, %0" ::"r"(compare));
    __asm volatile("msr cntp_ctl_el0, %0" ::"r"(1ULL));
}

static int aarch64_timer_init(uint32_t freq_hz) {
    uint32_t cpu = aarch64_smp_cpu_index();

    g_tick_freq_hz[cpu] = (freq_hz > 0u) ? freq_hz : 1000u;
    g_timer_cntfrq = bm_aarch64_read_cntfrq();
    if (g_timer_cntfrq == 0u) {
        g_timer_cntfrq = 62500000u;
    }
    g_timer_freq_hz = (uint32_t)g_timer_cntfrq;
    g_ticks[cpu] = 0u;

    if (bm_hal_cpu_is_bootstrap() && !g_gic_ready) {
        bm_aarch64_gic_dist_init();
        g_gic_ready = 1;
    }
    bm_aarch64_gic_cpu_init();
    bm_aarch64_timer_rearm(cpu);
    BM_LOGI(TAG_TIMER, "init: cpu=%u tick_hz=%u cntfrq=%u",
            (unsigned)cpu, (unsigned)g_tick_freq_hz[cpu],
            (unsigned)g_timer_freq_hz);
    return BM_OK;
}

static void aarch64_timer_stop(void) {
    g_tick_cb[aarch64_smp_cpu_index()] = NULL;
    __asm volatile("msr cntp_ctl_el0, %0" ::"r"(0ULL));
}

static uint32_t aarch64_timer_get_ticks(void) {
    return g_ticks[aarch64_smp_cpu_index()];
}

static uint32_t aarch64_timer_get_freq(void) {
    return g_tick_freq_hz[aarch64_smp_cpu_index()];
}

static void aarch64_timer_set_callback(void (*cb)(void)) {
    g_tick_cb[aarch64_smp_cpu_index()] = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    aarch64_timer_init,
    aarch64_timer_stop,
    aarch64_timer_get_ticks,
    aarch64_timer_get_freq,
    aarch64_timer_set_callback,
};

/** @brief tick ISR 内 FPU 现场保存区（当前 aarch64 no-op，接线预留）。 */
static uint8_t g_tick_cp0_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));

/**
 * @brief IRQ 顶层分发（由异常向量汇编调用）
 *
 * g_tick_cb 派发可能触达浮点回调，经 bm_arch_isr_fpu_enter/exit
 * （portable/arch/aarch64/bm_arch_isr_fpu.h）包裹；IRQ 入口汇编已保存
 * 完整 FP 现场（q0-q31/FPCR/FPSR），该守卫为 no-op 形态统一调用点。
 */
void bm_qemu_aarch64_irq_dispatch(void) {
    uint32_t cpu = aarch64_smp_cpu_index();
    uint32_t iar = GICC_IAR;
    uint32_t irq_id = iar & 0x3FFu;
    void (*cb)(void);
    unsigned cp_prev;

    if (irq_id == BM_AARCH64_TIMER_IRQ_ID) {
        g_ticks[cpu]++;
        cp_prev = bm_arch_isr_fpu_enter(g_tick_cp0_sa);
        cb = g_tick_cb[cpu];
        if (cb) {
            cb();
        }
        bm_arch_isr_fpu_exit(g_tick_cp0_sa, cp_prev);
        bm_aarch64_timer_rearm(cpu);
    }
    /* GICv2: 1022/1023 为保留/伪 IRQ ID，写 EOIR 在真机属 UNPREDICTABLE。 */
    if (irq_id < 1022u) {
        GICC_EOIR = iar;
    }
}

static int aarch64_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev;
    (void)config;
    BM_LOGI(TAG_UART, "init: PL011 @0x%08X", (unsigned)UART_BASE);
    return BM_OK;
}

static int aarch64_uart_send(const struct bm_hal_uart *dev,
                            const uint8_t *data, size_t len) {
    (void)dev;
    size_t i;
    uint32_t attempt;

    if (!data && len > 0u) {
        return BM_ERR_INVALID;
    }
    for (i = 0u; i < len; i++) {
        for (attempt = 0u; attempt < BM_QEMU_AARCH64_UART_TX_POLL_LIMIT;
             ++attempt) {
            if ((UART_FR & UART_FR_TXFF) == 0u) {
                break;
            }
            __asm volatile("yield");
        }
        if (attempt == BM_QEMU_AARCH64_UART_TX_POLL_LIMIT) {
            return BM_ERR_TIMEOUT;
        }
        UART_DR = (uint32_t)data[i];
    }
    return BM_OK;
}

static size_t aarch64_uart_recv(const struct bm_hal_uart *dev,
                               uint8_t *data, size_t max_len) {
    (void)dev;
    (void)data;
    (void)max_len;
    return 0u;
}

static void aarch64_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                        void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
}

static const struct bm_uart_driver_api g_uart_api = {
    aarch64_uart_init,
    aarch64_uart_send,
    aarch64_uart_recv,
    aarch64_uart_set_rx_callback,
};

/** @brief 默认控制台 UART 设备（统一实例模型，见 bm_hal_uart.h）。 */
const bm_hal_uart_t bm_uart_default = { &g_uart_api, NULL };

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

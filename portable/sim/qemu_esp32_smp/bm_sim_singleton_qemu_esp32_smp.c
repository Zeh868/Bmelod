/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_sim_singleton_qemu_esp32_smp.c
 * @brief QEMU ESP32 Xtensa SMP 单例驱动（TIMG0 定时器 / UART0 / 看门狗桩）
 * @maturity E1
 *
 * 临界区与内存屏障由 `bm_port_arch_xtensa` 提供。
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-16
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 * 2026-07-11       1.1            zeh            tick 回调派发接入 arch 层 FPU 守卫（bm_arch_isr_fpu.h，xtensa 路径当前仍为 no-op）
 * 2026-07-16       1.2            zeh            APP 核回调回本核派发：PRO ISR 只跑 cb[0]，APP 在自身 get_ticks 上下文按 tick 变化泵出（原 PRO 代跑全部核回调，APP 回调摸错核 per-CPU 状态）；配套 Level-1 向量改 callx4 蹦床 + 清 EXCM/置 WOE，出口改 rfe + PS/EPC1 软件自存自恢（XEA2 无 EPS1，rfi 1 还原不了原 PS；尾部以 EXCM=1 封死嵌套窗口）
 *
 * 2026-08-01       1.2            Codex            补全中文 Doxygen 合规注释
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_hal_uart.h"
#include "bm_drv_wdg.h"
#include "bm_log.h"
#include "bm_types.h"
#include "hal/bm_hal_cpu.h"
#include "xtensa/bm_arch_isr_fpu.h"

#include <stddef.h>
#include <stdint.h>

#define TAG_TIMER "hal_timer"
#define TAG_UART  "hal_uart"
#define TAG_WDG   "hal_wdg"

/** ESP32 Timer Group 0 */
#define TIMG0_BASE          0x3FF5F000UL
#define TIMG0_T0CONFIG      (*(volatile uint32_t *)(TIMG0_BASE + 0x000))
#define TIMG0_T0LO           (*(volatile uint32_t *)(TIMG0_BASE + 0x004))
#define TIMG0_T0HI           (*(volatile uint32_t *)(TIMG0_BASE + 0x008))
#define TIMG0_T0UPDATE       (*(volatile uint32_t *)(TIMG0_BASE + 0x00C))
#define TIMG0_T0ALARMLO      (*(volatile uint32_t *)(TIMG0_BASE + 0x010))
#define TIMG0_T0ALARMHI      (*(volatile uint32_t *)(TIMG0_BASE + 0x014))
#define TIMG0_T0LOADLO       (*(volatile uint32_t *)(TIMG0_BASE + 0x018))
#define TIMG0_T0LOADHI       (*(volatile uint32_t *)(TIMG0_BASE + 0x01C))
#define TIMG0_T0LOAD         (*(volatile uint32_t *)(TIMG0_BASE + 0x020))
#define TIMG0_INT_ENA        (*(volatile uint32_t *)(TIMG0_BASE + 0x098))
#define TIMG0_INT_RAW        (*(volatile uint32_t *)(TIMG0_BASE + 0x09C))
#define TIMG0_INT_CLR        (*(volatile uint32_t *)(TIMG0_BASE + 0x0A4))

/** T0CONFIG 位域 */
#define TIMG_T0_EN           (1u << 30)
#define TIMG_T0_INCREASE     (1u << 29)
#define TIMG_T0_AUTORELOAD   (1u << 28)
#define TIMG_T0_DIVIDER_EN   (1u << 13)
#define TIMG_T0_ALARM_EN     (1u << 31)

/** ESP32 UART0 */
#define UART0_BASE           0x3FF40000UL
#define UART0_FIFO           (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART0_STATUS         (*(volatile uint32_t *)(UART0_BASE + 0x01C))

/** STATUS bit16:24 = TXFIFO_CNT */
#define UART0_TXFIFO_CNT(status) (((status) >> 16) & 0xFFu)
#define UART0_TXFIFO_MAX       128u

/** QEMU ESP32 APB 定时器时钟（80 MHz） */
#define ESP32_SMP_TIMER_HZ   80000000u
/** TIMG0 T0 电平中断源号 */
#define ESP32_TG0_T0_INT_NUM 6u

static uint32_t g_tick_freq[BM_CONFIG_CPU_COUNT];
static volatile uint32_t g_ticks[BM_CONFIG_CPU_COUNT];
static void (*g_tick_cb[BM_CONFIG_CPU_COUNT])(void);
static uint32_t g_timer_alarm_ticks[BM_CONFIG_CPU_COUNT];
static int g_timer_armed[BM_CONFIG_CPU_COUNT];
/** APP 核在自身上下文泵回调时已见到的 tick（PRO 无 TIMG 中断可发，见 get_ticks） */
static uint32_t g_app_seen_tick[BM_CONFIG_CPU_COUNT];

/** PRO_CPU 拥有 TIMG0；从核共享 cpu0 tick */
#define ESP32_SMP_TIMER_OWNER_CPU  0u

/**
 * @brief 返回当前 CPU 索引（越界时回落 0）
 */
static uint32_t esp32_smp_cpu_index(void) {
    uint32_t cpu = bm_hal_cpu_id();

    return (cpu < BM_CONFIG_CPU_COUNT) ? cpu : 0u;
}

/**
 * @brief 使能 CPU 侧定时器中断（仅 PRO_CPU 操作硬件）
 */
static void esp32_smp_timer_enable_irq(void) {
    uint32_t ie;

    if (esp32_smp_cpu_index() != ESP32_SMP_TIMER_OWNER_CPU) {
        return;
    }
    __asm__ volatile("rsr.intenable %0" : "=a"(ie));
    ie |= (1u << ESP32_TG0_T0_INT_NUM);
    __asm__ volatile("wsr.intenable %0" :: "a"(ie));
}

/**
 * @brief 装载 TIMG0 T0 下一次 alarm（仅 PRO_CPU 写硬件）
 */
static void esp32_smp_timer_arm(uint32_t cpu) {
    uint32_t alarm = g_timer_alarm_ticks[cpu];

    if (cpu != ESP32_SMP_TIMER_OWNER_CPU) {
        return;
    }
    TIMG0_T0ALARMLO = alarm;
    TIMG0_T0ALARMHI = 0u;
    TIMG0_T0LOAD = 1u;
    g_timer_armed[cpu] = 1;
    TIMG0_INT_ENA = 1u;
    esp32_smp_timer_enable_irq();
}

/**
 * @brief 初始化当前逻辑 CPU 的定时器。
 * @param freq_hz 定时器频率，单位为 Hz；传入 0 时使用该端口默认频率。
 * @return 成功返回 BM_OK。
 */
static int esp32_smp_timer_init(uint32_t freq_hz) {
    uint32_t cpu = esp32_smp_cpu_index();
    uint32_t hz = (freq_hz > 0u) ? freq_hz : 1000u;
    uint32_t divider = 80u;

    g_tick_freq[cpu] = hz;
    g_ticks[cpu] = 0u;
    g_app_seen_tick[cpu] = 0u;
    g_timer_alarm_ticks[cpu] = ESP32_SMP_TIMER_HZ / divider / hz;
    if (g_timer_alarm_ticks[cpu] == 0u) {
        g_timer_alarm_ticks[cpu] = 1u;
    }

    /* APP_CPU 不操作 TIMG0；tick 由 PRO_CPU 中断驱动并镜像 */
    if (cpu != 0u) {
        BM_LOGI(TAG_TIMER, "init: cpu=%u freq_hz=%u (shared tick)", (unsigned)cpu, hz);
        return BM_OK;
    }

    TIMG0_T0CONFIG = 0u;
    TIMG0_T0LO = 0u;
    TIMG0_T0HI = 0u;
    TIMG0_T0UPDATE = 1u;
    TIMG0_INT_CLR = 1u;

    TIMG0_T0CONFIG = TIMG_T0_EN | TIMG_T0_INCREASE | TIMG_T0_AUTORELOAD
                     | TIMG_T0_DIVIDER_EN | TIMG_T0_ALARM_EN
                     | ((divider - 1u) & 0x1FFFu);

    esp32_smp_timer_arm(cpu);
    BM_LOGI(TAG_TIMER, "init: cpu=%u freq_hz=%u", (unsigned)cpu, hz);
    return BM_OK;
}

/**
 * @brief 停止定时器设备。
 */
static void esp32_smp_timer_stop(void) {
    uint32_t cpu = esp32_smp_cpu_index();

    g_tick_cb[cpu] = NULL;
    g_timer_armed[cpu] = 0;
    if (cpu != 0u) {
        return;
    }
    TIMG0_INT_ENA = 0u;
    TIMG0_T0CONFIG = 0u;
}

/**
 * @brief 读取当前逻辑 CPU 的定时器 tick 计数。
 * @return 当前逻辑 CPU 的定时器 tick 计数。
 */
static uint32_t esp32_smp_timer_get_ticks(void) {
    uint32_t cpu = esp32_smp_cpu_index();
    uint32_t t = g_ticks[0];

    /* APP 核无 TIMG 中断：在自身上下文泵出本核回调（替代 PRO 代跑——
       代跑会让 APP 注册的回调在 PRO 的 ISR 上下文执行，摸错核的
       per-CPU 状态）。框架 tick 查询路径在 APP 主循环定期到达。 */
    if (cpu != 0u && t != g_app_seen_tick[cpu]) {
        void (*cb)(void) = g_tick_cb[cpu];

        g_app_seen_tick[cpu] = t;
        if (cb) {
            cb();
        }
    }
    return t;
}

/**
 * @brief 读取当前定时器频率。
 * @return 定时器频率，单位为 Hz；设备无效时返回 0。
 */
static uint32_t esp32_smp_timer_get_freq(void) {
    return g_tick_freq[esp32_smp_cpu_index()];
}

/**
 * @brief 设置定时器回调。
 * @param cb tick 回调；传入 NULL 时解除绑定。
 */
static void esp32_smp_timer_set_callback(void (*cb)(void)) {
    g_tick_cb[esp32_smp_cpu_index()] = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    esp32_smp_timer_init,
    esp32_smp_timer_stop,
    esp32_smp_timer_get_ticks,
    esp32_smp_timer_get_freq,
    esp32_smp_timer_set_callback,
};

/** @brief tick ISR 内 FPU(CP0) 现场保存区（当前 xtensa no-op，接线预留）。 */
static uint8_t g_tick_cp0_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));

/**
 * @brief TIMG0 T0 电平中断服务（由 Level-1 向量调用，仅 PRO_CPU 硬件路径）
 *
 * g_tick_cb 派发链可能触达浮点回调，统一经 bm_arch_isr_fpu_enter/exit
 * （portable/arch/xtensa/bm_arch_isr_fpu.h）包裹；QEMU esp32 裸机路径下该
 * 守卫为 no-op（见该头文件「QEMU esp32 裸机平台真相」），此处接线只为统一
 * 调用点，行为不变。
 */
void qemu_esp32_smp_on_timer_irq(void) {
    void (*cb)(void);
    unsigned cp_prev;
    uint32_t n;

    if (esp32_smp_cpu_index() != 0u) {
        return;
    }
    if ((TIMG0_INT_RAW & 1u) == 0u) {
        return;
    }
    if (!g_timer_armed[0]) {
        TIMG0_INT_CLR = 1u;
        return;
    }
    TIMG0_INT_CLR = 1u;
    g_ticks[0]++;
    for (n = 1u; n < BM_CONFIG_CPU_COUNT; n++) {
        g_ticks[n] = g_ticks[0];
    }
    cp_prev = bm_arch_isr_fpu_enter(g_tick_cp0_sa);
    /* 只派发 PRO 本核回调；APP 核回调由 APP 在自身 get_ticks 上下文泵出 */
    cb = g_tick_cb[0];
    if (cb) {
        cb();
    }
    bm_arch_isr_fpu_exit(g_tick_cp0_sa, cp_prev);
    esp32_smp_timer_arm(0u);
}

/**
 * @brief 初始化UART端口。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param config 设备初始化配置；当前实现不使用该参数。
 * @return 成功返回 BM_OK。
 */
static int esp32_smp_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev;
    (void)config;
    BM_LOGI(TAG_UART, "init: UART0");
    return BM_OK;
}

/**
 * @brief 通过UART发送数据。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param data 待发送数据缓冲区。
 * @param len 缓冲区中的有效数据长度，单位为字节。
 * @return 成功返回 BM_OK。
 */
static int esp32_smp_uart_send(const struct bm_hal_uart *dev,
                            const uint8_t *data, size_t len) {
    (void)dev;
    size_t i;

    if (!data || len == 0u) {
        return BM_OK;
    }
    for (i = 0u; i < len; i++) {
        while (UART0_TXFIFO_CNT(UART0_STATUS) >= UART0_TXFIFO_MAX) {
        }
        UART0_FIFO = (uint32_t)data[i];
    }
    return BM_OK;
}

/**
 * @brief 从UART接收数据。
 * @param dev UART 设备实例；当前实现不使用该参数。
 * @param data 接收数据缓冲区；当前仿真桩不读写该缓冲区。
 * @param max_len 接收缓冲区容量，单位为字节。
 * @return 实际写入接收缓冲区的字节数；无数据或参数无效时返回 0。
 */
static size_t esp32_smp_uart_recv(const struct bm_hal_uart *dev,
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
static void esp32_smp_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                        void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
}

static const struct bm_uart_driver_api g_uart_api = {
    esp32_smp_uart_init,
    esp32_smp_uart_send,
    esp32_smp_uart_recv,
    esp32_smp_uart_set_rx_callback,
};

/** @brief 默认控制台 UART 设备（统一实例模型，见 bm_hal_uart.h）。 */
const bm_hal_uart_t bm_uart_default = { &g_uart_api, NULL };

/**
 * @brief 初始化看门狗仿真桩。
 * @param timeout_ms 看门狗超时时间，单位为毫秒；当前仿真桩不使用该值。
 * @return 成功返回 BM_OK。
 */
static int esp32_smp_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    BM_LOGI(TAG_WDG, "init: stub");
    return BM_OK;
}

/**
 * @brief 喂养看门狗仿真桩。
 */
static void esp32_smp_wdg_feed(void) {
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    esp32_smp_wdg_init,
    esp32_smp_wdg_feed,
};

/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_vendor_singleton_esp32_idf.c
 * @brief ESP32 后端的 timer / UART / WDT 单例实现（Phase 3：驱动层收尾）
 *
 * 本文件使用 IDF 5.2.3 的底层头文件、ROM 打印和看门狗寄存器封装。
 *
 * Phase 2 变更（相较 Phase 1）：
 *   - 系统 tick 改为 TIMERG0 timer0 + esp_intr_alloc 实现周期中断，
 *     在 ISR 上下文调用注册的回调（满足 bm_hal_timer 契约）。
 *   - 删除 `bm_vendor_timer_advance` 及其轮询推进模型。
 *   - `get_ticks` 直接返回自由运行计数器（软件累积计数，每 ISR +1）。
 *   - TIMERG1 保留给 WDT；系统 tick 使用 TIMERG0 timer0，避免冲突。
 *   - UART/WDG 维持现状。
 *
 * Phase 3 变更：
 *   - WDT 实现额外保留 esp_task_wdt_config_t 类型注释，记录 IDF 5 Task WDT
 *     API 接口；硬件 MWDT（TIMERG1）实现不变，运行在裸机环境（无 FreeRTOS 调度器）。
 *   - Timer bus clock 使能改为 PERIPH_RCC_ATOMIC 正规写法（真实 IDF 构建走宏，
 *     compilecheck ffreestanding 走条件退化路径）。
 *
 * @note IDF 5 Task WDT（esp_task_wdt_init/reconfigure）依赖 FreeRTOS 调度器与
 *       esp_timer，在本裸机路径下不可调用。此处仅声明类型用于兼容性备档；
 *       实际看门狗由 MWDT LL 寄存器直接控制（见 WDT 驱动实现节）。
 *       待硬件：若日后迁移到 IDF FreeRTOS 应用路径，可切换为 esp_task_wdt_init。
 *
 * @author zeh (china_qzh@163.com)
 * @version 3.7
 * @date 2026-07-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            迁入 vendor
 * 2026-06-19       1.2            zeh            改为裸机底层实现
 * 2026-06-19       2.0            zeh            Phase 2：timer_group LL + ISR 驱动
 * 2026-06-19       3.0            zeh            Phase 3：RCC 宏正规化 + task WDT 类型归档
 * 2026-06-26       3.1            zeh            添加 bm_hal_uptime_ns_raw()（路线图 #9 时间基统一 1a）
 * 2026-07-11       3.2            zeh            tick ISR 加 FPU 协处理器守卫，修复 10kHz 电流环回调触发 Coprocessor 异常崩溃
 * 2026-07-11       3.3            zeh            esp32_uart_recv 实现 UART0 RX FIFO 非阻塞轮询读（uart_ll），修 shell 无法输入
 * 2026-07-12       3.4            zeh            tick 中断注册去掉 ESP_INTR_FLAG_IRAM：
 *                                                回调链在 flash，flash 写窗口（WiFi PHY
 *                                                校准/NVS commit）内触发即 cache panic
 *                                                循环重启（真机实证）；改为窗口内自动
 *                                                延迟，丢拍由 LET/wcet 账目如实体现
 * 2026-07-12       3.5            zeh            去除 TG0 组级复位（误伤 esp_timer LACT
 *                                                时基致 WiFi 连接 INT WDT），改窄范围清
 *                                                timer0 中断
 * 2026-07-13       3.6            zeh            tick 中断级别 LEVEL3→LEVEL2：Plan B 把 FOC
 *                                                电流环 bind 到 PWM TEZ ISR（LEVEL2）后该
 *                                                ISR 跑满 FPU，LEVEL3 tick 抢占之致嵌套中断
 *                                                破坏寄存器窗口/FPU 上下文 LoadProhibited
 *                                                崩溃（真机 addr2line 实证）；降至与 TEZ 同级
 *                                                消除嵌套，三请求正好占满 3 条 LEVEL2 线
 * 2026-07-15       3.7            zeh            esp_intr_alloc_intrstatus 接住 esp_err_t，
 *                                                失败 esp_rom_printf 并 return BM_ERR_IO
 *                                                fail-fast（tick 分配失败=系统无节拍，
 *                                                静默继续比崩溃更危险）
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_hal_uptime.h"
#include "bm_types.h"
#include "xtensa/bm_arch_isr_fpu.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "hal/mwdt_ll.h"
#include "hal/timer_ll.h"
/*
 * UART0 RX FIFO 轮询读依赖 uart_ll_get_rxfifo_len / uart_ll_read_rxfifo。
 * hal/uart_ll.h 及其全部传递依赖（hal/misc.h、soc/uart_reg.h、soc/uart_struct.h、
 * soc/dport_reg.h、hal/uart_types.h）均在 pack 注入的 IDF 头清单
 * （cmake/bm_sdk_esp32_idf.cmake）与 _compilecheck harness 的 include 路径内，
 * 故直接 #include（与 timer_ll/mwdt_ll 同一惯例），无需 esp_timer 式前向声明。
 */
#include "hal/uart_ll.h"
#include "soc/timer_group_struct.h"
#include "soc/interrupts.h"
#include "esp_intr_alloc.h"

/*
 * 单调时钟后端依赖 esp_timer_get_time()（自启动起的 µs 计数，int64，单调，ISR 安全）。
 *
 * 此处对其作前向声明而非 #include "esp_timer.h"：本 vendor 静态库的 IDF 头路径
 * 由 pack（cmake/bm_sdk_esp32_idf.cmake 的 bm_sdk_esp32_idf_apply）显式注入，
 * 该清单及独立编译检查（_compilecheck）的 harness 均未纳入 esp_timer 组件的
 * include 目录，直接 #include 会找不到头文件。esp_timer_get_time 签名稳定
 * （IDF 5.2.3：int64_t esp_timer_get_time(void)），前向声明即可编译；其符号在
 * 最终镜像链接期由 esp_timer 核心组件提供（应用 main 已 REQUIRES esp_timer）。
 */
extern int64_t esp_timer_get_time(void);

/*
 * IDF 5 Task Watchdog Timer（TWDT）API 类型归档。
 *
 * esp_task_wdt_config_t 定义于 esp_task_wdt.h（IDF 5.x），其头文件依赖
 * freertos/FreeRTOS.h，在本裸机路径（-ffreestanding，无 RTOS 运行时）下
 * 不可直接包含。此处复制类型定义（与 IDF 5.2.3 声明严格对齐），仅供
 * 编译期类型兼容性存档；实际 WDT 由下方 MWDT LL 驱动直接控制。
 *
 * 真实 IDF FreeRTOS 应用路径下，应改为：
 *   #include "esp_task_wdt.h"
 *   esp_task_wdt_config_t twdt_cfg = { ... };
 *   esp_task_wdt_init(&twdt_cfg);
 *
 * 待硬件：迁移到 IDF 应用路径后替换此块，并移除下方 MWDT LL WDT 实现。
 */
#if !defined(ESP_PLATFORM) || defined(BM_ESP32_COMPILECHECK_FFREESTANDING)
/**
 * @brief ESP-IDF 5 Task Watchdog Timer（TWDT）配置结构体（裸机路径本地前向定义）。
 *
 * 与 esp_task_wdt.h（IDF 5.2.3）声明严格对齐：
 *   - timeout_ms：超时时间（ms）
 *   - idle_core_mask：监控哪些 core 的 idle 任务（bitmask）
 *   - trigger_panic：超时时是否触发 panic
 *
 * @note 仅在 compilecheck（-ffreestanding）或非 ESP_PLATFORM 环境下生效；
 *       真实 IDF 构建时由 esp_task_wdt.h 提供正式声明，此块被跳过。
 */
typedef struct {
    uint32_t timeout_ms;     /**< TWDT 超时时间（毫秒） */
    uint32_t idle_core_mask; /**< 被监控的 core idle 任务 bitmask（bit i = core i） */
    bool     trigger_panic;  /**< 超时时触发 panic */
} esp_task_wdt_config_t;
#else
/* 真实 IDF FreeRTOS 构建：通过正规头文件引入类型（依赖调度器，裸机下禁用）。 */
/* #include "esp_task_wdt.h" */
/* 裸机路径仍然不包含，仅保留此注释说明切换路径。 */
/**
 * @brief esp_task_wdt_config_t 在真实 IDF 构建路径下由 esp_task_wdt.h 提供。
 *        裸机路径（BM_ESP32_BAREMETAL=1）不引入该头，以下 typedef 保证类型可用。
 */
typedef struct {
    uint32_t timeout_ms;     /**< TWDT 超时时间（毫秒） */
    uint32_t idle_core_mask; /**< 被监控的 core idle 任务 bitmask（bit i = core i） */
    bool     trigger_panic;  /**< 超时时触发 panic */
} esp_task_wdt_config_t;
#endif /* !ESP_PLATFORM || BM_ESP32_COMPILECHECK_FFREESTANDING */

/** @brief 系统 tick 使用 TIMERG0，WDT 使用 TIMERG1，避免冲突。 */
#define BM_VENDOR_TICK_TIMER_GROUP    0
/** @brief TIMERG0 内的 timer0 编号。 */
#define BM_VENDOR_TICK_TIMER_NUM      0u

/**
 * @brief APB 时钟频率（Hz），用于计算 timer 分频。
 *
 * ESP32 默认 APB = 80 MHz（CPU 240 MHz 时钟树默认值）。
 */
#define BM_VENDOR_APB_FREQ_HZ         80000000u

/** @brief 看门狗分频后每毫秒对应 tick 数。 */
#define BM_VENDOR_WDT_TICKS_PER_MS    2u

/* ---------- 全局状态 ---------- */
static void (*g_tick_callback)(void);
static void (*g_rx_callback)(uint8_t c);
static uint32_t    g_tick_freq_hz;
static uint32_t    g_tick_count;
static uint8_t     g_uart_ready;
static uint8_t     g_wdt_ready;
static intr_handle_t g_tick_intr_handle;

/**
 * @brief tick ISR 内 FPU(CP0) 现场保存区（16 字节对齐）。
 *
 * 供 bm_arch_isr_fpu_enter/exit（portable/arch/xtensa/bm_arch_isr_fpu.h）
 * 保存/恢复被打断代码的浮点现场，使 g_tick_callback 派发链（经 hrt_dispatch
 * 触达 10kHz 电流环等浮点回调）在 ISR 内安全执行。单例定时器只有一份 tick
 * ISR，故仅需一份保存区（不与 PWM ISR 的 cp0_sa 共享，二者互不嵌套）。
 * 无 FPU 芯片或非 ESP_PLATFORM 路径上 BM_ARCH_ISR_FPU_SA_SIZE=1，仅占位、
 * 守卫为 no-op。
 */
static uint8_t g_tick_cp0_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));

/* ---------- Timer ISR ---------- */

/**
 * @brief TIMERG0 timer0 周期中断服务函数。
 *
 * 每次 timer alarm 触发时：
 *   1. 清除中断标志并重新使能 alarm（自动重载已配置，此处重使能 alarm）
 *   2. 递增 tick 计数
 *   3. 在 FPU 守卫内调用注册的 tick 回调
 *
 * 铁律步骤 1-2（清中断、计数）在 FPU 守卫之外先完成，不受浮点开销影响；
 * g_tick_callback 经 hrt_dispatch 可能派发到 10kHz 电流环等浮点回调（FOC
 * current_step 等），ESP 中断上下文默认禁用 FPU(CP0)，故整段派发须包在
 * bm_arch_isr_fpu_enter/exit 之间，顺序铁律见 bm_arch_isr_fpu.h：
 * 开 CP0 → 存现场 → 跑浮点 → 复现场 → 还原 CPENABLE。
 *
 * @param arg 未使用（NULL）。
 */
static void IRAM_ATTR bm_vendor_tick_isr(void *arg)
{
    timg_dev_t *hw;
    unsigned    cp_prev;

    (void)arg;
    hw = TIMER_LL_GET_HW(BM_VENDOR_TICK_TIMER_GROUP);

    /* 清除中断并重新使能 alarm */
    timer_ll_clear_intr_status(hw, TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM));
    timer_ll_enable_alarm(hw, BM_VENDOR_TICK_TIMER_NUM, true);

    /* 递增 tick 计数 */
    g_tick_count++;

    /* 调用注册的 tick 回调（FPU 守卫内，见函数头注释） */
    cp_prev = bm_arch_isr_fpu_enter(g_tick_cp0_sa);
    if (g_tick_callback != NULL) {
        g_tick_callback();
    }
    bm_arch_isr_fpu_exit(g_tick_cp0_sa, cp_prev);
}

/* ---------- timer 驱动实现 ---------- */

/**
 * @brief 初始化系统 tick 定时器（TIMERG0 timer0 + ISR）。
 *
 * APB = 80 MHz，分频 = APB / freq_hz（要求可整除）。
 * alarm 自动重载到 0；每次 ISR 重置 alarm 值实现周期触发。
 *
 * @param freq_hz tick 频率（Hz）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效。
 */
static int esp32_timer_init(uint32_t freq_hz)
{
    timg_dev_t *hw;
    uint32_t    divider;
    uint64_t    alarm_val;

    if (freq_hz == 0u) {
        return BM_ERR_INVALID;
    }

    /* 计算分频值：divider = APB / freq_hz，要求 divider >= 2 */
    divider = BM_VENDOR_APB_FREQ_HZ / freq_hz;
    if (divider < 2u) {
        divider = 2u;
    }

    /* alarm_val = 1 tick 后触发（因分频后 1 tick = 1/freq_hz） */
    alarm_val = 1u;

    hw = TIMER_LL_GET_HW(BM_VENDOR_TICK_TIMER_GROUP);

    /*
     * 使能 TIMERG0 总线时钟。
     * 使用 BM_PERIPH_RCC_ATOMIC_BEGIN/END 宏（bm_vendor_esp32_idf_compat.h）：
     *   - 真实 IDF 构建（ESP_PLATFORM）：展开为 PERIPH_RCC_ATOMIC(){...} 临界块。
     *   - compilecheck/freestanding：展开为带守卫变量声明的普通块（满足 IDF LL 宏要求）。
     */
    /*
     * 仅使能总线时钟，刻意不做组级复位 timer_ll_reset_register(TG0)：
     *   timer_ll_reset_register(BM_VENDOR_TICK_TIMER_GROUP) 是对整个 Timer
     *   Group 0 的 DPORT 组级复位，会连带复位 esp_timer 在 ESP32 上占用的
     *   TG0 LACT 微秒时基（CONFIG_ESP_TIMER_IMPL_TG0_LAC）。LACT 一旦被复位，
     *   esp_timer 时基失准、已 arm 的定时器永不触发/移除，WiFi 一连上狂 arm
     *   使有序链表暴涨，timer_insert 持锁遍历长链表吃满 300ms 触发 CPU0
     *   Interrupt WDT（真机 2026-07-12 实证：station 连上 SoftAP 即崩）。
     *   timer0 所需初态由下方逐寄存器显式配置独立达成，不依赖组复位；
     *   组复位唯一有用副作用（清 timer0 pending 中断位）用窄范围清中断精确补回。
     * timer_ll_enable_bus_clock 幂等、只动总线时钟门控，不扰 LACT，保留。
     */
    BM_PERIPH_RCC_ATOMIC_BEGIN
        timer_ll_enable_bus_clock(BM_VENDOR_TICK_TIMER_GROUP, true);
    BM_PERIPH_RCC_ATOMIC_END

    /* 停止计数 */
    timer_ll_enable_counter(hw, BM_VENDOR_TICK_TIMER_NUM, false);

    /*
     * 替代组复位唯一有用副作用：清 timer0 pending 中断位。
     * 窄范围只清 timer0 的 alarm 事件，不碰 LACT / 整组，避免误伤 esp_timer 时基。
     */
    timer_ll_clear_intr_status(hw, TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM));

    /* 配置分频、向上计数、自动重载 */
    timer_ll_set_clock_prescale(hw, BM_VENDOR_TICK_TIMER_NUM, divider);
    timer_ll_set_count_direction(hw, BM_VENDOR_TICK_TIMER_NUM, GPTIMER_COUNT_UP);
    timer_ll_enable_auto_reload(hw, BM_VENDOR_TICK_TIMER_NUM, true);

    /* 设置 reload 值为 0，alarm 为 1（每分频周期触发一次） */
    timer_ll_set_reload_value(hw, BM_VENDOR_TICK_TIMER_NUM, 0u);
    timer_ll_set_alarm_value(hw, BM_VENDOR_TICK_TIMER_NUM, alarm_val);
    timer_ll_trigger_soft_reload(hw, BM_VENDOR_TICK_TIMER_NUM);

    /* 使能中断 */
    timer_ll_enable_intr(hw, TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM), true);
    timer_ll_enable_alarm(hw, BM_VENDOR_TICK_TIMER_NUM, true);

    /* 注册 ISR（若已注册先释放） */
    if (g_tick_intr_handle != NULL) {
        (void)esp_intr_free(g_tick_intr_handle);
        g_tick_intr_handle = NULL;
    }
    {
        volatile void *status_reg;
        uint32_t       status_mask;
        esp_err_t      intr_rc;

        status_reg  = timer_ll_get_intr_status_reg(hw);
        status_mask = TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM);
        /*
         * 刻意不带 ESP_INTR_FLAG_IRAM：tick 回调链（bm_hrt 派发→tt_schedule→
         * 控制 LET）整体位于 flash，若以 IRAM-safe 注册，flash 写入窗口
         * （WiFi PHY 校准/NVS commit 等，cache 关闭）内中断照常触发会跳入
         * flash 地址，触发 "Cache disabled but cached memory region accessed"
         * panic（真机 2026-07-12 实证：WiFi 起网必炸循环重启）。去掉该标志
         * 后 flash 操作期间本中断被 IDF 自动延迟、窗口结束恢复——控制环丢拍
         * 由 LET staleness/wcet deadline-miss 账目如实体现；出力期与 flash
         * 写的互斥由业务门控保证（web spec §6 R1：armed 拒 save/reset）。
         */
        /*
         * 中断级别 LEVEL2：与两个 PWM TEZ ISR（bm_vendor_pwm_esp32_idf.c
         * 的 esp_intr_alloc，ESP_INTR_FLAG_LEVEL2）严格同级。Plan B 把 FOC
         * 电流环 bind 到 TEZ ISR 后，该 ISR 不再"职责轻"而是跑满 FPU
         * （sqrtf / foc current_step）；若本 tick 保持 LEVEL3，会抢占正在算
         * FPU 的 TEZ ISR，嵌套中断破坏 Xtensa 寄存器窗口 / FPU 协处理器
         * 上下文，致 LoadProhibited 崩溃（真机 2026-07-13 addr2line 实证：
         * _xt_medint3 → tick → tt_bus_publish 打断 current_hw_isr_axis 的
         * sqrtf）。同级则互不抢占、串行执行，从根上消除嵌套（等价于 v3.5
         * 拆分级别之前 tick/TEZ 同为 LEVEL3 的"无嵌套"不变量，因 LEVEL3 只
         * 有 2 条通用线容不下 3 个请求，故同级落点改取 LEVEL2）。
         * PWM0 + PWM1 + tick 三请求正好占满 3 条 LEVEL2 通用线（intno
         * 19/20/21）；若日后同核再增 LEVEL2 消费者需重评容量（见
         * bm_vendor_pwm_esp32_idf.c changelog v3.5 / v3.6）。
         */
        intr_rc = esp_intr_alloc_intrstatus(
            ETS_TG0_T0_LEVEL_INTR_SOURCE,
            ESP_INTR_FLAG_LEVEL2,
            (uint32_t)(uintptr_t)status_reg,
            status_mask,
            bm_vendor_tick_isr,
            NULL,
            &g_tick_intr_handle);
        if (intr_rc != ESP_OK) {
            /* tick 分配失败 = 系统无节拍，静默继续比崩溃更危险，必须 fail-fast */
            esp_rom_printf("bm_vendor: tick intr alloc failed rc=%d\n",
                           (int)intr_rc);
            return BM_ERR_IO;
        }
    }

    /* 启动计数 */
    timer_ll_enable_counter(hw, BM_VENDOR_TICK_TIMER_NUM, true);

    g_tick_freq_hz = freq_hz;
    g_tick_count   = 0u;
    return BM_OK;
}

/**
 * @brief 停止系统 tick 定时器并释放 ISR。
 */
static void esp32_timer_stop(void)
{
    timg_dev_t *hw;

    hw = TIMER_LL_GET_HW(BM_VENDOR_TICK_TIMER_GROUP);
    timer_ll_enable_counter(hw, BM_VENDOR_TICK_TIMER_NUM, false);
    timer_ll_enable_intr(hw, TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM), false);

    if (g_tick_intr_handle != NULL) {
        (void)esp_intr_free(g_tick_intr_handle);
        g_tick_intr_handle = NULL;
    }

    g_tick_callback = NULL;
    g_tick_freq_hz  = 0u;
    g_tick_count    = 0u;
}

/**
 * @brief 读取当前 tick 计数值（自由运行，每 ISR +1）。
 *
 * @note Phase 2：不再触发轮询推进，直接返回 ISR 维护的 g_tick_count。
 *
 * @return 当前 tick 计数。
 */
static uint32_t esp32_timer_get_ticks(void)
{
    return g_tick_count;
}

/**
 * @brief 查询 tick 频率。
 * @return 当前配置的 tick 频率（Hz）。
 */
static uint32_t esp32_timer_get_freq(void)
{
    return g_tick_freq_hz;
}

/**
 * @brief 注册 tick 回调（在 ISR 上下文按 freq_hz 周期调用）。
 * @param cb tick 回调函数；NULL 取消注册。
 */
static void esp32_timer_set_callback(void (*cb)(void))
{
    g_tick_callback = cb;
}

/** @brief timer 驱动 API 表。 */
const struct bm_timer_driver_api bm_drv_timer_api = {
    esp32_timer_init,
    esp32_timer_stop,
    esp32_timer_get_ticks,
    esp32_timer_get_freq,
    esp32_timer_set_callback,
};

/* ---------- UART 驱动实现（维持 Phase 1 实现） ---------- */

/**
 * @brief 初始化 UART（仅记录就绪标志，ROM 打印无需显式初始化）。
 * @param config 未使用。
 * @return BM_OK。
 */
static int esp32_uart_init(void *config)
{
    (void)config;
    g_uart_ready = 1u;
    return BM_OK;
}

/**
 * @brief 通过 ROM 打印输出字节流。
 * @param data 数据指针。
 * @param len  数据长度。
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未初始化。
 */
static int esp32_uart_send(const uint8_t *data, size_t len)
{
    size_t i;

    if (data == NULL) {
        return BM_ERR_INVALID;
    }
    if (g_uart_ready == 0u) {
        return BM_ERR_NOT_INIT;
    }
    for (i = 0u; i < len; ++i) {
        esp_rom_printf("%c", (int)data[i]);
    }
    return BM_OK;
}

/**
 * @brief 接收字节：UART0 RX FIFO 非阻塞轮询读。
 *
 * 直接经 uart_ll 读硬件 RX FIFO（uart_ll_get_rxfifo_len 查可读字节数，
 * uart_ll_read_rxfifo 逐字节搬出），有多少读多少（不超过 @p max_len），
 * FIFO 空立即返回 0，不阻塞不等待——契合裸机主循环轮询（bm_shell_poll →
 * bm_hal_console_read → 本函数）的节奏。
 *
 * @note 只读 FIFO，不动波特率/引脚/时钟配置：UART0 由 boot ROM 按 115200
 *       配好并被 esp_rom_printf（TX 侧）复用，RX 侧同一硬件口。
 * @note 刻意不用 IDF uart_driver_install（会起后台中断+环形缓冲+任务，
 *       与本 vendor 裸机零调度架构不搭）；FIFO 深 128 字节，主循环 tick
 *       级轮询下人工敲键不会溢出。
 *
 * @param data    接收缓冲区。
 * @param max_len 缓冲区容量（字节）。
 * @return 实际读出的字节数；无数据/未初始化/参数无效时为 0。
 */
static size_t esp32_uart_recv(uint8_t *data, size_t max_len)
{
    uint32_t avail;
    uint32_t n;

    if (data == NULL || max_len == 0u) {
        return 0u;
    }
    if (g_uart_ready == 0u) {
        return 0u;
    }

    avail = uart_ll_get_rxfifo_len(&UART0);
    if (avail == 0u) {
        return 0u;
    }
    n = (avail < (uint32_t)max_len) ? avail : (uint32_t)max_len;
    uart_ll_read_rxfifo(&UART0, data, n);
    return (size_t)n;
}

/**
 * @brief 注册 RX 回调（裸机模式下保存但不主动触发）。
 * @param cb RX 回调。
 */
static void esp32_uart_set_rx_callback(void (*cb)(uint8_t c))
{
    g_rx_callback = cb;
    (void)g_rx_callback;
}

/** @brief UART 驱动 API 表。 */
const struct bm_uart_driver_api bm_drv_uart_api = {
    esp32_uart_init,
    esp32_uart_send,
    esp32_uart_recv,
    esp32_uart_set_rx_callback,
};

/* ---------- WDT 驱动实现（维持 Phase 1 实现，使用 TIMERG1） ---------- */

/**
 * @brief 返回 WDT 硬件实例（TIMERG1）。
 *
 * TIMERG0 用于系统 tick，TIMERG1 专用于 WDT，避免资源冲突。
 *
 * @return TIMERG1 寄存器基址。
 */
static inline timg_dev_t *bm_vendor_wdt_hw(void)
{
    return &TIMERG1;
}

/**
 * @brief 初始化 MWDT 看门狗（TIMERG1）。
 * @param timeout_ms 超时时间（ms）；0 时默认 5000 ms。
 * @return BM_OK 成功；否则为平台错误码。
 */
static int esp32_wdg_init(uint32_t timeout_ms)
{
    timg_dev_t *hw;
    uint32_t    timeout_ticks;

    if (timeout_ms == 0u) {
        timeout_ms = 5000u;
    }

    hw            = bm_vendor_wdt_hw();
    timeout_ticks = timeout_ms * BM_VENDOR_WDT_TICKS_PER_MS;

    mwdt_ll_write_protect_disable(hw);
    mwdt_ll_disable(hw);
    mwdt_ll_set_clock_source(hw, MWDT_CLK_SRC_APB);
    mwdt_ll_set_prescaler(hw, MWDT_LL_DEFAULT_CLK_PRESCALER);
    mwdt_ll_disable_stage(hw, WDT_STAGE1);
    mwdt_ll_disable_stage(hw, WDT_STAGE2);
    mwdt_ll_disable_stage(hw, WDT_STAGE3);
    mwdt_ll_config_stage(hw, WDT_STAGE0, timeout_ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    mwdt_ll_set_edge_intr(hw, false);
    mwdt_ll_set_level_intr(hw, false);
    mwdt_ll_enable(hw);
    mwdt_ll_write_protect_enable(hw);

    g_wdt_ready = 1u;
    return BM_OK;
}

/**
 * @brief 喂狗（重置 MWDT 计数器）。
 */
static void esp32_wdg_feed(void)
{
    timg_dev_t *hw;

    if (g_wdt_ready == 0u) {
        return;
    }
    hw = bm_vendor_wdt_hw();
    mwdt_ll_write_protect_disable(hw);
    mwdt_ll_feed(hw);
    mwdt_ll_write_protect_enable(hw);
}

/** @brief WDG 驱动 API 表。 */
const struct bm_wdg_driver_api bm_drv_wdg_api = {
    esp32_wdg_init,
    esp32_wdg_feed,
};

/* ---------- 单调时钟后端（路线图 #9 时间基统一 1a） ---------- */

/**
 * @brief ESP-IDF 单调时钟后端（esp_timer 高精度定时器）。
 *
 * 包装 IDF 的 esp_timer_get_time()：该函数返回自系统启动起经过的微秒数
 * （int64_t，单调不减，ISR 安全），此处换算为纳秒（× 1000u）以满足框架
 * `bm_hal_uptime_ns_raw()` 契约，供上层 `bm_uptime_ns()` / `bm_uptime_us()` 使用。
 *
 * @note esp_timer 上电即非负且单调；防御性钳位负值后强转 uint64_t，
 *       × 1000u 在 uint64 域不溢出（~584 年级别）。
 *
 * @return 自系统启动起经过的纳秒数（uint64_t，单调不减）。
 */
uint64_t bm_hal_uptime_ns_raw(void)
{
    int64_t us = esp_timer_get_time();

    if (us < 0) {
        us = 0;
    }
    return (uint64_t)us * 1000u;
}

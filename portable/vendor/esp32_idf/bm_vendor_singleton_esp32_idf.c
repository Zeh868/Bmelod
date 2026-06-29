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
 * @version 3.1
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            迁入 vendor
 * 2026-06-19       1.2            zeh            改为裸机底层实现
 * 2026-06-19       2.0            zeh            Phase 2：timer_group LL + ISR 驱动
 * 2026-06-19       3.0            zeh            Phase 3：RCC 宏正规化 + task WDT 类型归档
 * 2026-06-26       3.1            zeh            添加 bm_hal_uptime_ns_raw()（路线图 #9 时间基统一 1a）
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_hal_uptime.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "hal/mwdt_ll.h"
#include "hal/timer_ll.h"
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

/* ---------- Timer ISR ---------- */

/**
 * @brief TIMERG0 timer0 周期中断服务函数。
 *
 * 每次 timer alarm 触发时：
 *   1. 清除中断标志并重新使能 alarm（自动重载已配置，此处重使能 alarm）
 *   2. 递增 tick 计数
 *   3. 调用注册的 tick 回调
 *
 * @param arg 未使用（NULL）。
 */
static void IRAM_ATTR bm_vendor_tick_isr(void *arg)
{
    timg_dev_t *hw;

    (void)arg;
    hw = TIMER_LL_GET_HW(BM_VENDOR_TICK_TIMER_GROUP);

    /* 清除中断并重新使能 alarm */
    timer_ll_clear_intr_status(hw, TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM));
    timer_ll_enable_alarm(hw, BM_VENDOR_TICK_TIMER_NUM, true);

    /* 递增 tick 计数 */
    g_tick_count++;

    /* 调用注册的 tick 回调 */
    if (g_tick_callback != NULL) {
        g_tick_callback();
    }
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
    BM_PERIPH_RCC_ATOMIC_BEGIN
        timer_ll_enable_bus_clock(BM_VENDOR_TICK_TIMER_GROUP, true);
        timer_ll_reset_register(BM_VENDOR_TICK_TIMER_GROUP);
    BM_PERIPH_RCC_ATOMIC_END

    /* 停止计数 */
    timer_ll_enable_counter(hw, BM_VENDOR_TICK_TIMER_NUM, false);

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

        status_reg  = timer_ll_get_intr_status_reg(hw);
        status_mask = TIMER_LL_EVENT_ALARM(BM_VENDOR_TICK_TIMER_NUM);
        (void)esp_intr_alloc_intrstatus(
            ETS_TG0_T0_LEVEL_INTR_SOURCE,
            ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM,
            (uint32_t)(uintptr_t)status_reg,
            status_mask,
            bm_vendor_tick_isr,
            NULL,
            &g_tick_intr_handle);
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
 * @brief 接收字节（裸机模式下不支持，返回 0）。
 * @param data    未使用。
 * @param max_len 未使用。
 * @return 0（无可用字节）。
 */
static size_t esp32_uart_recv(uint8_t *data, size_t max_len)
{
    (void)data;
    (void)max_len;
    return 0u;
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

/**
 * @file bm_vendor_singleton_esp32_idf.c
 * @brief ESP32 裸机后端的 timer / UART / WDT 单例实现
 *
 * 该文件只使用 IDF 的底层头文件、ROM 打印和看门狗寄存器封装，
 * 不依赖调度器、队列或高级外设驱动。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            迁入 vendor
 * 2026-06-19       1.2            zeh          改为裸机底层实现
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_vendor_esp32_idf_compat.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "hal/mwdt_ll.h"
#include "soc/timer_group_struct.h"

/** @brief ESP32 默认 CPU 主频，用于 cycle->tick 粗略换算。 */
#define BM_VENDOR_ESP32_CPU_FREQ_HZ      240000000u
/** @brief 看门狗分频后每个 tick 对应的微秒数。 */
#define BM_VENDOR_WDT_TICKS_PER_MS       2u

static void (*g_tick_callback)(void);
static void (*g_rx_callback)(uint8_t c);
static uint32_t g_tick_freq_hz;
static uint32_t g_tick_period_cycles;
static uint32_t g_tick_count;
static uint32_t g_tick_start_cycle;
static uint32_t g_tick_last_cycle;
static uint8_t g_tick_dispatching;
static uint8_t g_uart_ready;
static uint8_t g_wdt_ready;

/**
 * @brief PWM 裸机子步推进入口。
 */
extern void bm_vendor_pwm_esp32_idf_timer_tick(void);

/**
 * @brief 按当前计数器推进周期回调。
 *
 * 该实现依赖主循环或上层轮询触发，以保持裸机后端无 RTOS 依赖。
 */
static void bm_vendor_timer_advance(void) {
    uint32_t now_cycle;
    uint32_t delta_cycles;

    if (g_tick_freq_hz == 0u || g_tick_period_cycles == 0u || g_tick_dispatching != 0u) {
        return;
    }

    now_cycle = esp_cpu_get_cycle_count();
    delta_cycles = now_cycle - g_tick_last_cycle;
    while (delta_cycles >= g_tick_period_cycles) {
        delta_cycles -= g_tick_period_cycles;
        g_tick_last_cycle += g_tick_period_cycles;
        g_tick_count++;
        if (g_tick_callback != NULL && g_tick_dispatching == 0u) {
            g_tick_dispatching = 1u;
            g_tick_callback();
            g_tick_dispatching = 0u;
        }
        bm_vendor_pwm_esp32_idf_timer_tick();
        bm_vendor_pwm_esp32_idf_timer_tick();
    }
}

/**
 * @brief 计算裸机计时器周期对应的 CPU cycle 数。
 */
static uint32_t bm_vendor_timer_cycles_per_tick(uint32_t freq_hz) {
    uint32_t cycles;

    if (freq_hz == 0u) {
        return 0u;
    }
    cycles = BM_VENDOR_ESP32_CPU_FREQ_HZ / freq_hz;
    return cycles;
}

static int esp32_timer_init(uint32_t freq_hz) {
    if (freq_hz == 0u) {
        return BM_ERR_INVALID;
    }

    g_tick_freq_hz = freq_hz;
    g_tick_period_cycles = bm_vendor_timer_cycles_per_tick(freq_hz);
    if (g_tick_period_cycles == 0u) {
        return BM_ERR_INVALID;
    }
    g_tick_start_cycle = esp_cpu_get_cycle_count();
    g_tick_last_cycle = g_tick_start_cycle;
    g_tick_count = 0u;
    return BM_OK;
}

static void esp32_timer_stop(void) {
    g_tick_callback = NULL;
    g_tick_freq_hz = 0u;
    g_tick_period_cycles = 0u;
    g_tick_count = 0u;
    g_tick_start_cycle = 0u;
    g_tick_last_cycle = 0u;
    g_tick_dispatching = 0u;
}

static uint32_t esp32_timer_get_ticks(void) {
    bm_vendor_timer_advance();
    return g_tick_count;
}

static uint32_t esp32_timer_get_freq(void) {
    return g_tick_freq_hz;
}

static void esp32_timer_set_callback(void (*cb)(void)) {
    g_tick_callback = cb;
}

const struct bm_timer_driver_api bm_drv_timer_api = {
    esp32_timer_init,
    esp32_timer_stop,
    esp32_timer_get_ticks,
    esp32_timer_get_freq,
    esp32_timer_set_callback,
};

static int esp32_uart_init(void *config) {
    (void)config;
    g_uart_ready = 1u;
    return BM_OK;
}

static int esp32_uart_send(const uint8_t *data, size_t len) {
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

static size_t esp32_uart_recv(uint8_t *data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0u;
}

static void esp32_uart_set_rx_callback(void (*cb)(uint8_t c)) {
    g_rx_callback = cb;
    (void)g_rx_callback;
}

const struct bm_uart_driver_api bm_drv_uart_api = {
    esp32_uart_init,
    esp32_uart_send,
    esp32_uart_recv,
    esp32_uart_set_rx_callback,
};

/**
 * @brief 返回 WDT 硬件实例。
 */
static inline timg_dev_t *bm_vendor_wdt_hw(void) {
    return &TIMERG1;
}

static int esp32_wdg_init(uint32_t timeout_ms) {
    timg_dev_t *hw;
    uint32_t timeout_ticks;

    if (timeout_ms == 0u) {
        timeout_ms = 5000u;
    }

    hw = bm_vendor_wdt_hw();
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

static void esp32_wdg_feed(void) {
    timg_dev_t *hw;

    if (g_wdt_ready == 0u) {
        return;
    }
    hw = bm_vendor_wdt_hw();
    mwdt_ll_write_protect_disable(hw);
    mwdt_ll_feed(hw);
    mwdt_ll_write_protect_enable(hw);
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    esp32_wdg_init,
    esp32_wdg_feed,
};

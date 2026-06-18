/**
 * @file bm_vendor_singleton_esp32_idf.c
 * @brief 灯哥平衡车主控板 ESP32-WROOM-32E vendor 单例驱动（定时器 / UART / 看门狗）
 *
 * 临界区与内存屏障由 `bm_port_arch_xtensa` 提供。
 * 电机、IMU、PWM、ADC 等外设待用户提供板级引脚表后扩展。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            从 sdk_esp32_idf singleton 拆分
 * 2026-06-15       1.1            zeh            兼容 ESP-IDF 5 task WDT 配置 API
 *
 */
#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_drv_wdg.h"
#include "bm_hal_instances_esp32wroom32e.h"
#include "bm_hal_sdk_esp32.h"
#include "bm_log.h"
#include "bm_types.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BM_UART_DEFAULT_BAUD 115200u
#define TAG_WDG              "hal_wdg"
#define TAG_UART             "hal_uart"
#define TAG_TIMER            "hal_timer"

static void (*g_tick_callback)(void);
static uint32_t g_tick_freq;
static void (*g_rx_callback)(uint8_t c);
static int g_uart_installed;
static int g_wdt_enabled;
static gptimer_handle_t g_tick_timer;

static bool esp32_tick_alarm_cb(gptimer_handle_t timer,
                                const gptimer_alarm_event_data_t *edata,
                                void *user_ctx) {
    (void)timer;
    (void)edata;
    (void)user_ctx;
    if (g_tick_callback) {
        g_tick_callback();
    }
    return false;
}

static int esp32_timer_init(uint32_t freq_hz) {
    gptimer_config_t timer_cfg;
    gptimer_alarm_config_t alarm_cfg;
    gptimer_event_callbacks_t cbs;
    uint64_t alarm_ticks;
    esp_err_t err;

    if (freq_hz == 0u) {
        return BM_ERR_INVALID;
    }
    if (g_tick_timer != NULL) {
        gptimer_stop(g_tick_timer);
        gptimer_disable(g_tick_timer);
        gptimer_del_timer(g_tick_timer);
        g_tick_timer = NULL;
    }

    g_tick_freq = freq_hz;
    memset(&timer_cfg, 0, sizeof(timer_cfg));
    timer_cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_cfg.direction = GPTIMER_COUNT_UP;
    timer_cfg.resolution_hz = BM_ESP32WROOM32E_TICK_TIMER_RESOLUTION_HZ;

    err = gptimer_new_timer(&timer_cfg, &g_tick_timer);
    if (err != ESP_OK) {
        BM_LOGE(TAG_TIMER, "gptimer_new_timer: %d", (int)err);
        return BM_ERR_IO;
    }

    memset(&cbs, 0, sizeof(cbs));
    cbs.on_alarm = esp32_tick_alarm_cb;
    err = gptimer_register_event_callbacks(g_tick_timer, &cbs, NULL);
    if (err != ESP_OK) {
        return BM_ERR_IO;
    }

    alarm_ticks = BM_ESP32WROOM32E_TICK_TIMER_RESOLUTION_HZ / (uint64_t)freq_hz;
    if (alarm_ticks == 0u) {
        return BM_ERR_INVALID;
    }

    memset(&alarm_cfg, 0, sizeof(alarm_cfg));
    alarm_cfg.alarm_count = alarm_ticks;
    alarm_cfg.reload_count = 0;
    alarm_cfg.flags.auto_reload_on_alarm = true;
    err = gptimer_set_alarm_action(g_tick_timer, &alarm_cfg);
    if (err != ESP_OK) {
        return BM_ERR_IO;
    }

    err = gptimer_enable(g_tick_timer);
    if (err != ESP_OK) {
        return BM_ERR_IO;
    }
    err = gptimer_start(g_tick_timer);
    if (err != ESP_OK) {
        return BM_ERR_IO;
    }
    return BM_OK;
}

static void esp32_timer_stop(void) {
    if (g_tick_timer != NULL) {
        gptimer_stop(g_tick_timer);
        gptimer_disable(g_tick_timer);
        gptimer_del_timer(g_tick_timer);
        g_tick_timer = NULL;
    }
    g_tick_callback = NULL;
}

static uint32_t esp32_timer_get_ticks(void) {
    uint64_t count = 0u;

    if (g_tick_timer != NULL) {
        (void)gptimer_get_raw_count(g_tick_timer, &count);
    }
    return (uint32_t)count;
}

static uint32_t esp32_timer_get_freq(void) {
    return g_tick_freq;
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
    uint32_t baud = BM_UART_DEFAULT_BAUD;
    uart_config_t uart_cfg;
    esp_err_t err;

    if (config != NULL) {
        baud = *(const uint32_t *)config;
        if (baud == 0u) {
            baud = BM_UART_DEFAULT_BAUD;
        }
    }

    if (!g_uart_installed) {
        memset(&uart_cfg, 0, sizeof(uart_cfg));
        uart_cfg.baud_rate = (int)baud;
        uart_cfg.data_bits = UART_DATA_8_BITS;
        uart_cfg.parity = UART_PARITY_DISABLE;
        uart_cfg.stop_bits = UART_STOP_BITS_1;
        uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_cfg.source_clk = UART_SCLK_DEFAULT;

        err = uart_driver_install(BM_ESP32WROOM32E_UART_PORT, 256, 0, 0, NULL, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            BM_LOGE(TAG_UART, "uart_driver_install: %d", (int)err);
            return BM_ERR_IO;
        }
        err = uart_param_config(BM_ESP32WROOM32E_UART_PORT, &uart_cfg);
        if (err != ESP_OK) {
            return BM_ERR_IO;
        }
        g_uart_installed = 1;
    } else {
        err = uart_set_baudrate(BM_ESP32WROOM32E_UART_PORT, baud);
        if (err != ESP_OK) {
            return BM_ERR_IO;
        }
    }
    return BM_OK;
}

static int esp32_uart_send(const uint8_t *data, size_t len) {
    int written;

    if (data == NULL) {
        return BM_ERR_INVALID;
    }
    written = uart_write_bytes(BM_ESP32WROOM32E_UART_PORT, (const char *)data, len);
    if (written < 0) {
        return BM_ERR_IO;
    }
    return BM_OK;
}

static size_t esp32_uart_recv(uint8_t *data, size_t max_len) {
    int n;

    if (data == NULL || max_len == 0u) {
        return 0u;
    }
    n = uart_read_bytes(BM_ESP32WROOM32E_UART_PORT, data, max_len, 0);
    if (n < 0) {
        return 0u;
    }
    return (size_t)n;
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

static int esp32_wdg_init(uint32_t timeout_ms) {
    esp_err_t err;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    uint32_t timeout_s;
#else
    esp_task_wdt_config_t wdt_cfg;
#endif

    if (timeout_ms == 0u) {
        timeout_ms = 5000u;
    }
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    timeout_s = (timeout_ms + 999u) / 1000u;
    if (timeout_s == 0u) {
        timeout_s = 1u;
    }
#endif

    err = esp_task_wdt_deinit();
    (void)err;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    err = esp_task_wdt_init(timeout_s, true);
#else
    memset(&wdt_cfg, 0, sizeof(wdt_cfg));
    wdt_cfg.timeout_ms = timeout_ms;
    wdt_cfg.idle_core_mask = 0u;
    wdt_cfg.trigger_panic = true;
    err = esp_task_wdt_init(&wdt_cfg);
#endif
    if (err != ESP_OK) {
        BM_LOGE(TAG_WDG, "esp_task_wdt_init: %d", (int)err);
        return BM_ERR_IO;
    }
    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        return BM_ERR_IO;
    }
    g_wdt_enabled = 1;
    return BM_OK;
}

static void esp32_wdg_feed(void) {
    if (g_wdt_enabled) {
        (void)esp_task_wdt_reset();
    }
}

const struct bm_wdg_driver_api bm_drv_wdg_api = {
    esp32_wdg_init,
    esp32_wdg_feed,
};

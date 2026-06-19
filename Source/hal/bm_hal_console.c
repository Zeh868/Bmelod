/**
 * @file bm_hal_console.c
 * @brief Console HAL 分发层（按通道选择编译期后端）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 *
 */
#include "bm_config.h"
#include "hal/bm_hal_console.h"
#include "bm_types.h"
#include "hal/bm_hal_cpu.h"

#if BM_CONFIG_HARD_RT_PROFILE && \
    (BM_CONFIG_CONSOLE_LOG_BACKEND == BM_CONSOLE_BACKEND_STDIO || \
     BM_CONFIG_CONSOLE_CLI_BACKEND == BM_CONSOLE_BACKEND_STDIO)
#error "hard RT profile forbids stdio console backend"
#endif

int bm_console_stub_init(void);
int bm_console_stub_write(const uint8_t *data, size_t len);
size_t bm_console_stub_read(uint8_t *data, size_t max_len);

int bm_console_stdio_init(void);
int bm_console_stdio_write(const uint8_t *data, size_t len);
size_t bm_console_stdio_read(uint8_t *data, size_t max_len);

int bm_console_uart_init(void);
int bm_console_uart_write(const uint8_t *data, size_t len);
size_t bm_console_uart_read(uint8_t *data, size_t max_len);

int bm_console_rtt_init(void);
int bm_console_rtt_write(const uint8_t *data, size_t len);
size_t bm_console_rtt_read(uint8_t *data, size_t max_len);

static int g_log_backend_inited;
static int g_cli_backend_inited;

/**
 * @brief 按后端 ID 初始化
 */
static int console_init_backend(int backend) {
    switch (backend) {
    case BM_CONSOLE_BACKEND_NONE:
        return BM_OK;
    case BM_CONSOLE_BACKEND_STDIO:
        return bm_console_stdio_init();
    case BM_CONSOLE_BACKEND_UART:
        return bm_console_uart_init();
    case BM_CONSOLE_BACKEND_RTT:
        return bm_console_rtt_init();
    default:
        return BM_ERR_INVALID;
    }
}

/**
 * @brief 按后端 ID 写出
 */
static int console_write_backend(int backend, const uint8_t *data, size_t len) {
    switch (backend) {
    case BM_CONSOLE_BACKEND_NONE:
        return bm_console_stub_write(data, len);
    case BM_CONSOLE_BACKEND_STDIO:
        return bm_console_stdio_write(data, len);
    case BM_CONSOLE_BACKEND_UART:
        return bm_console_uart_write(data, len);
    case BM_CONSOLE_BACKEND_RTT:
        return bm_console_rtt_write(data, len);
    default:
        return BM_ERR_INVALID;
    }
}

/**
 * @brief 按后端 ID 读取
 */
static size_t console_read_backend(int backend, uint8_t *data, size_t max_len) {
    switch (backend) {
    case BM_CONSOLE_BACKEND_NONE:
        return bm_console_stub_read(data, max_len);
    case BM_CONSOLE_BACKEND_STDIO:
        return bm_console_stdio_read(data, max_len);
    case BM_CONSOLE_BACKEND_UART:
        return bm_console_uart_read(data, max_len);
    case BM_CONSOLE_BACKEND_RTT:
        return bm_console_rtt_read(data, max_len);
    default:
        return 0u;
    }
}

/**
 * @brief 解析通道对应的后端 ID
 */
static int console_backend_for_ch(bm_console_ch_t ch) {
    return (ch == BM_CONSOLE_LOG) ?
        (int)BM_CONFIG_CONSOLE_LOG_BACKEND :
        (int)BM_CONFIG_CONSOLE_CLI_BACKEND;
}

/**
 * @brief 多核 CLI 是否允许本核访问
 */
static int console_cli_allowed_this_cpu(void) {
#if BM_CONFIG_CPU_COUNT > 1u && BM_CONFIG_CONSOLE_MP_CLI_BOOTSTRAP_ONLY
    return bm_hal_cpu_is_bootstrap();
#else
    return 1;
#endif
}

int bm_hal_console_init(void) {
    int rc;

    if (!g_log_backend_inited) {
        rc = console_init_backend((int)BM_CONFIG_CONSOLE_LOG_BACKEND);
        if (rc != BM_OK) {
            return rc;
        }
        g_log_backend_inited = 1;
    }
    if ((int)BM_CONFIG_CONSOLE_CLI_BACKEND !=
        (int)BM_CONFIG_CONSOLE_LOG_BACKEND && !g_cli_backend_inited) {
        rc = console_init_backend((int)BM_CONFIG_CONSOLE_CLI_BACKEND);
        if (rc != BM_OK) {
            return rc;
        }
        g_cli_backend_inited = 1;
    } else if (!g_cli_backend_inited) {
        g_cli_backend_inited = g_log_backend_inited;
    }
    return BM_OK;
}

int bm_hal_console_write(bm_console_ch_t ch, const uint8_t *data, size_t len) {
    int backend;

    if (ch != BM_CONSOLE_LOG && ch != BM_CONSOLE_CLI) {
        return BM_ERR_INVALID;
    }
    if (!data && len > 0u) {
        return BM_ERR_INVALID;
    }
    if (len == 0u) {
        return BM_OK;
    }
#if BM_CONFIG_HARD_RT_PROFILE
    if (ch == BM_CONSOLE_CLI) {
        return BM_ERR_NOT_SUPPORTED;
    }
#endif
    if (ch == BM_CONSOLE_CLI && !console_cli_allowed_this_cpu()) {
        return BM_ERR_NOT_SUPPORTED;
    }
    backend = console_backend_for_ch(ch);
    return console_write_backend(backend, data, len);
}

size_t bm_hal_console_read(bm_console_ch_t ch, uint8_t *data, size_t max_len) {
    int backend;

    if (ch != BM_CONSOLE_CLI) {
        return 0u;
    }
    if (!data || max_len == 0u) {
        return 0u;
    }
#if BM_CONFIG_HARD_RT_PROFILE
    return 0u;
#endif
    if (!console_cli_allowed_this_cpu()) {
        return 0u;
    }
    backend = console_backend_for_ch(ch);
    return console_read_backend(backend, data, max_len);
}

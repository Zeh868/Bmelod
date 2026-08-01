/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_console.c
 * @brief Console HAL 分发层（按通道选择编译期后端）
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            初版
 * 2026-08-01       1.0            Codex           补全 Doxygen 合规注释
 *
 */
#include "bm_config.h"
#include "hal/bm_hal_console.h"
#include "bm_types.h"
#include "hal/bm_hal_cpu.h"
#include "bm/common/bm_critical_wrap.h"

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

/* 通道后端初始化状态：0=未初始化 1=初始化中 2=完成 */
#define CONSOLE_STATE_IDLE  0
#define CONSOLE_STATE_BUSY  1
#define CONSOLE_STATE_DONE  2

static volatile int g_log_backend_state;
static volatile int g_cli_backend_state;

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

/**
 * @brief 初始化单个通道后端（三态标志，临界区只护标志）
 *
 * 慢速后端 init 在开中断下执行，避免关中断窗口随后端实现拉长；
 * 失败回滚为 IDLE 可重试，并发第二调用者拿到 BM_ERR_BUSY。
 *
 * @param state   通道三态标志指针
 * @param backend 后端 ID
 * @return BM_OK 成功（含幂等命中 DONE）；BM_ERR_BUSY 他方正在初始化
 */
static int console_init_channel(volatile int *state, int backend) {
    int rc;
    bm_irq_state_t irq_state = BM_CRITICAL_ENTER();

    if (*state == CONSOLE_STATE_DONE) {
        BM_CRITICAL_EXIT(irq_state);
        return BM_OK;
    }
    if (*state == CONSOLE_STATE_BUSY) {
        BM_CRITICAL_EXIT(irq_state);
        return BM_ERR_BUSY;
    }
    *state = CONSOLE_STATE_BUSY;
    BM_CRITICAL_EXIT(irq_state);

    rc = console_init_backend(backend); /* 慢操作在开中断下执行 */

    irq_state = BM_CRITICAL_ENTER();
    *state = (rc == BM_OK) ? CONSOLE_STATE_DONE : CONSOLE_STATE_IDLE;
    BM_CRITICAL_EXIT(irq_state);
    return rc;
}

/**
 * @brief 初始化 console HAL（日志 + CLI 后端）
 *
 * @details 并发契约：本函数幂等且只执行一次真实后端初始化。多核/多任务
 * 环境下调用须由调用方保证串行；临界区只保护三态标志的读改，真实后端
 * 初始化在开中断下执行。并发第二调用者返回 BM_ERR_BUSY（符合启动期单一
 * 上下文的实际用法），初始化失败可重试。
 */
int bm_hal_console_init(void) {
    int rc;

    rc = console_init_channel(&g_log_backend_state,
                              (int)BM_CONFIG_CONSOLE_LOG_BACKEND);
    if (rc != BM_OK) {
        return rc;
    }

    if ((int)BM_CONFIG_CONSOLE_CLI_BACKEND ==
        (int)BM_CONFIG_CONSOLE_LOG_BACKEND) {
        /* CLI 与 LOG 共用后端：随 log 通道一并就绪 */
        bm_irq_state_t irq_state = BM_CRITICAL_ENTER();
        if (g_cli_backend_state != CONSOLE_STATE_DONE) {
            g_cli_backend_state = g_log_backend_state;
        }
        BM_CRITICAL_EXIT(irq_state);
        return BM_OK;
    }
    return console_init_channel(&g_cli_backend_state,
                                (int)BM_CONFIG_CONSOLE_CLI_BACKEND);
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

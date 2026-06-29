/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_shell.c
 * @brief 轻量级非阻塞串口命令行实现
 *
 * 字符回显、退格处理与命令分词执行；通过 Console CLI 通道收发。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#include "bm_shell.h"
#include "hal/bm_hal_console.h"
#include "bm_log.h"

#include <string.h>

/**
 * @brief 简易字符串比较（减少对完整 libc 的依赖）
 *
 * @param a 第一个字符串
 * @param b 第二个字符串
 * @return 相等返回 0；不等返回字符差值
 */
static int _strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * @brief 经 Console CLI 通道发送以 NUL 结尾的字符串
 *
 * @param s 字符串指针
 */
static void _puts(const char *s) {
    if (s) {
        (void)bm_hal_console_write(BM_CONSOLE_CLI,
                                   (const uint8_t *)s, strlen(s));
    }
}

/**
 * @brief 初始化 Shell 上下文与命令表
 *
 * @param shell Shell 实例指针
 */
void bm_shell_init(bm_shell_t *shell) {
    if (!shell) return;
    memset(shell->buf, 0, sizeof(shell->buf));
    shell->cursor = 0;
    shell->cmd_count = 0;
    shell->swallow_lf = 0;
    BM_LOGI("shell", "init");
}

/**
 * @brief 注册一条 Shell 命令
 *
 * @param shell Shell 实例指针
 * @param name 命令名称
 * @param fn 命令处理函数
 * @param help 帮助文本（可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NO_MEM 命令表已满
 */
int bm_shell_register(bm_shell_t *shell, const char *name,
                      bm_shell_cmd_fn_t fn, const char *help) {
    size_t name_len;

    if (!shell || !name || !fn) return BM_ERR_INVALID;
    name_len = strlen(name);
    if (name_len == 0u || name_len >= BM_CONFIG_SHELL_MAX_NAME_LEN) {
        return BM_ERR_INVALID;
    }
    if (shell->cmd_count >= BM_CONFIG_SHELL_MAX_CMDS) {
        BM_LOGW("shell", "register table full");
        return BM_ERR_NO_MEM;
    }

    memcpy(shell->cmd_names[shell->cmd_count], name, name_len + 1u);
    shell->cmds[shell->cmd_count].name =
        shell->cmd_names[shell->cmd_count];
    shell->cmds[shell->cmd_count].fn = fn;
    shell->cmds[shell->cmd_count].help = help;
    shell->cmd_count++;
    BM_LOGD("shell", "cmd '%s' registered", name);
    return BM_OK;
}

/**
 * @brief 就地分词命令行，返回参数个数
 *
 * @param line 可修改的命令行缓冲区
 * @param argv 参数指针数组
 * @param max_argv 最大参数个数
 * @return 解析得到的 argc
 */
static int _tokenize(char *line, char *argv[], int max_argv) {
    int argc = 0;
    char *p = line;

    /* 就地分词：将分隔符替换为 \0，argv 指向各参数首地址 */
    while (*p) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (!*p) break;
        if (argc >= max_argv) {
            return -1;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p++ = '\0';
        }
    }
    return argc;
}

/**
 * @brief 解析并执行一条命令行
 *
 * @param shell Shell 实例指针
 * @param line 可修改的命令行缓冲区
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_FOUND 未知命令；其他为命令返回值
 */
int bm_shell_exec(bm_shell_t *shell, char *line) {
    if (!shell || !line) return BM_ERR_INVALID;

    char *argv[BM_CONFIG_SHELL_MAX_ARGS];
    int argc = _tokenize(line, argv, BM_CONFIG_SHELL_MAX_ARGS);
    if (argc < 0) {
        BM_LOGW("shell", "too many arguments");
        return BM_ERR_INVALID;
    }
    if (argc == 0) {
        return BM_OK;
    }
    if (shell->cmd_count > BM_CONFIG_SHELL_MAX_CMDS) {
        return BM_ERR_INVALID;
    }

    for (uint8_t i = 0; i < shell->cmd_count; i++) {
        if (_strcmp(argv[0], shell->cmds[i].name) == 0) {
            BM_LOGD("shell", "exec '%s'", argv[0]);
            return shell->cmds[i].fn(argc, argv);
        }
    }

    BM_LOGW("shell", "unknown cmd '%s'", argv[0]);
    _puts("unknown command: ");
    _puts(argv[0]);
    _puts("\r\n");
    return BM_ERR_NOT_FOUND;
}

/**
 * @brief 处理单个输入字符（回显、退格、回车执行）
 *
 * @param shell Shell 实例指针
 * @param c 输入字符
 */
void bm_shell_feed(bm_shell_t *shell, char c) {
    if (!shell) return;

    if (c == '\n' && shell->swallow_lf) {
        shell->swallow_lf = 0;
        return;
    }
    shell->swallow_lf = 0;

    if (c == '\b' || c == 0x7F) {
        if (shell->cursor > 0) {
            shell->cursor--;
            shell->buf[shell->cursor] = '\0';
            _puts("\b \b");
        }
        return;
    }

    if (c >= 0x20 && c < 0x7F) {
        if (shell->cursor < BM_CONFIG_SHELL_BUF_SIZE - 1) {
            shell->buf[shell->cursor++] = c;
            (void)bm_hal_console_write(BM_CONSOLE_CLI, (const uint8_t *)&c, 1u);
        } else {
            BM_LOGW("shell", "line buffer full");
            (void)bm_hal_console_write(BM_CONSOLE_CLI,
                                       (const uint8_t *)"\a", 1u);
        }
        return;
    }

    if (c == '\r' || c == '\n') {
        if (c == '\r') {
            shell->swallow_lf = 1;
        }
        _puts("\r\n");
        if (shell->cursor > 0) {
            shell->buf[shell->cursor] = '\0';
            bm_shell_exec(shell, shell->buf);
            shell->cursor = 0;
        }
        _puts("$ ");
    }
}

#ifndef BM_CONFIG_SHELL_MAX_CHARS_PER_POLL
#define BM_CONFIG_SHELL_MAX_CHARS_PER_POLL  64u
#endif

/**
 * @brief 轮询 Console CLI 接收并逐字符喂入 Shell
 *
 * 每轮最多处理 BM_CONFIG_SHELL_MAX_CHARS_PER_POLL 字符，
 * 保证主循环有界执行。剩余字符在后续 poll 中处理。
 *
 * @param shell Shell 实例指针
 */
void bm_shell_poll(bm_shell_t *shell) {
    if (!shell) return;
    uint8_t c;
    uint32_t remaining = BM_CONFIG_SHELL_MAX_CHARS_PER_POLL;
    while (remaining > 0u &&
           bm_hal_console_read(BM_CONSOLE_CLI, &c, 1u) == 1u) {
        bm_shell_feed(shell, (char)c);
        remaining--;
    }
}

/**
 * @brief 通过 Shell 输出通道打印字符串
 *
 * @param s 以 NUL 结尾的字符串
 */
void bm_shell_puts(const char *s) {
    if (s) _puts(s);
}

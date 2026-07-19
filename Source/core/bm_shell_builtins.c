/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_shell_builtins.c
 * @brief Shell 内建命令组实现：param/log/ver/uptime（批 P）
 *
 * `param` 是 bm_param 的命令行前端（get/set/list/save/reset）；`log`
 * 调整运行期日志级别阈值（编译期 BM_CONFIG_LOG_LEVEL 是天花板）；
 * `ver` 打印固件编译时间；`uptime` 打印开机运行时长。所有输出经
 * bm_shell_puts；每次 snprintf 后强制补 NUL（MinGW/msvcrt 截断返回
 * -1 不补 NUL 的坑）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-18
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：shell 内建命令组）
 * 2026-07-11       1.1            zeh            set 越界提示区分 BM_ERR_INVALID；
 *                                                 list 每行追加有界参数的 [min..max]
 * 2026-07-18       1.2            zeh            shell 交互批③：param 命令挂 Tab
 *                                                 补全器——argv_idx==1 补子命令关键字
 *                                                 （list/get/set/save/reset），
 *                                                 argv_idx==2 补参数名（动态取自
 *                                                 bm_param_count/bm_param_desc_at，
 *                                                 非写死列表）
 *
 */
#include "bm/core/bm_shell_builtins.h"
#include "bm/core/bm_shell.h"
#include "bm/core/bm_param.h"
#include "bm/common/bm_uptime.h"
#include "bm/common/bm_types.h"
#include "bm_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief "param" 命令：get/set/list/save/reset 子命令分发（bm_param 前端）。
 */
static int cmd_param(int argc, char *argv[])
{
    char buf[160];

    if (argc < 2) {
        bm_shell_puts("usage: param <list|get|set|save|reset> [name] [value]\r\n");
        return BM_ERR_INVALID;
    }

    if (strcmp(argv[1], "list") == 0) {
        uint16_t i, n = bm_param_count();

        if (n == 0u) {
            bm_shell_puts("param: no table registered\r\n");
            return BM_ERR_NOT_INIT;
        }
        for (i = 0u; i < n; ++i) {
            const bm_param_desc_t *d = bm_param_desc_at(i);
            float v = 0.0f;
            char range_buf[32] = "";

            (void)bm_param_value_at(i, &v);
            if (d->min < d->max) {
                (void)snprintf(range_buf, sizeof(range_buf), " [%g..%g]",
                               (double)d->min, (double)d->max);
                range_buf[sizeof(range_buf) - 1u] = '\0';
            }
            (void)snprintf(buf, sizeof(buf), "  %s = %.6f%s%s%s\r\n",
                           d->name, (double)v,
                           (d->pkey != NULL) ? " [kv]" : "",
                           ((d->flags & BM_PARAM_FLAG_REBOOT) != 0u) ? " [reboot]" : "",
                           range_buf);
            buf[sizeof(buf) - 1u] = '\0';
            bm_shell_puts(buf);
        }
        return BM_OK;
    }

    if (strcmp(argv[1], "get") == 0) {
        float v = 0.0f;
        int rc;

        if (argc < 3) {
            bm_shell_puts("usage: param get <name>\r\n");
            return BM_ERR_INVALID;
        }
        rc = bm_param_get(argv[2], &v);
        if (rc != BM_OK) {
            bm_shell_puts("param: not found\r\n");
            return rc;
        }
        (void)snprintf(buf, sizeof(buf), "%s = %.6f\r\n", argv[2], (double)v);
        buf[sizeof(buf) - 1u] = '\0';
        bm_shell_puts(buf);
        return BM_OK;
    }

    if (strcmp(argv[1], "set") == 0) {
        char *endptr = NULL;
        float v;
        int rc;

        if (argc < 4) {
            bm_shell_puts("usage: param set <name> <value>\r\n");
            return BM_ERR_INVALID;
        }
        v = strtof(argv[3], &endptr);
        if (endptr == argv[3] || *endptr != '\0') {
            bm_shell_puts("param: invalid float value\r\n");
            return BM_ERR_INVALID;
        }
        rc = bm_param_set(argv[2], v);
        if (rc == BM_PARAM_REBOOT_REQUIRED) {
            bm_shell_puts("param: stored (reboot required)\r\n");
            return BM_OK;
        }
        if (rc == BM_ERR_INVALID) {
            bm_shell_puts("param: value out of range or not finite\r\n");
            return rc;
        }
        if (rc != BM_OK) {
            bm_shell_puts("param: set failed (not found?)\r\n");
            return rc;
        }
        (void)snprintf(buf, sizeof(buf), "%s = %.6f (live)\r\n", argv[2], (double)v);
        buf[sizeof(buf) - 1u] = '\0';
        bm_shell_puts(buf);
        return BM_OK;
    }

    if (strcmp(argv[1], "save") == 0) {
        int rc = bm_param_save();

        if (rc < 0) {
            bm_shell_puts("param: save failed\r\n");
            return rc;
        }
        (void)snprintf(buf, sizeof(buf), "param: %d saved to kv\r\n", rc);
        buf[sizeof(buf) - 1u] = '\0';
        bm_shell_puts(buf);
        return BM_OK;
    }

    if (strcmp(argv[1], "reset") == 0) {
        int rc = bm_param_reset();

        if (rc == BM_ERR_BUSY) {
            bm_shell_puts("param: reset refused (guard active)\r\n");
            return rc;
        }
        if (rc != BM_OK) {
            bm_shell_puts("param: reset failed\r\n");
            return rc;
        }
        bm_shell_puts("param: factory defaults restored, kv cleared\r\n");
        return BM_OK;
    }

    bm_shell_puts("usage: param <list|get|set|save|reset> [name] [value]\r\n");
    return BM_ERR_INVALID;
}

/**
 * @brief "param" 命令 Tab 补全器：子命令关键字 + 参数名（动态取自 bm_param）。
 *
 * argv_idx==1（"param <Tab>"）：补子命令关键字 list/get/set/save/reset
 * （静态表，命令集固定）；argv_idx==2（"param set/get <Tab>"）：补参数名，
 * 逐条经 bm_param_count()/bm_param_desc_at() 动态枚举当前登记表（不写死
 * 名字列表，表变化自动跟随）。按约定自行做前缀过滤（strncmp），只 emit
 * 满足 prefix 的候选；其余 argv_idx（如 list/save/reset 之后，本无需
 * 参数）不产生候选，交由上层判 0 候选响铃。
 *
 * @param argv_idx   当前词参数序号（1=子命令，2=参数名）
 * @param prefix     当前词已输入前缀
 * @param prefix_len 前缀长度
 * @param emit       候选发射回调
 * @param emit_ctx   透传给 emit 的收集器上下文
 * @param user_ctx   未使用（NULL）
 */
static void complete_param(uint8_t argv_idx, const char *prefix, uint8_t prefix_len,
                           bm_shell_complete_emit_fn_t emit, void *emit_ctx, void *user_ctx)
{
    (void)user_ctx;

    if (argv_idx == 1u) {
        static const char *const k_sub[] = { "list", "get", "set", "save", "reset" };
        uint8_t i;

        for (i = 0u; i < (uint8_t)(sizeof(k_sub) / sizeof(k_sub[0])); ++i) {
            if (strncmp(k_sub[i], prefix, (size_t)prefix_len) == 0) {
                emit(emit_ctx, k_sub[i]);
            }
        }
        return;
    }

    if (argv_idx == 2u) {
        uint16_t i, n = bm_param_count();

        for (i = 0u; i < n; ++i) {
            const bm_param_desc_t *d = bm_param_desc_at(i);

            if (d != NULL && strncmp(d->name, prefix, (size_t)prefix_len) == 0) {
                emit(emit_ctx, d->name);
            }
        }
    }
}

/**
 * @brief 解析日志级别字面量：先试 5 个级别名，再试单字符 '0'..'4'。
 *
 * @param s   待解析字符串
 * @param out 输出级别（成功时写入）
 * @return BM_OK 解析成功；BM_ERR_INVALID 无法识别
 */
static int parse_level(const char *s, bm_log_level_t *out)
{
    if (strcmp(s, "error") == 0) { *out = BM_LOG_ERROR; return BM_OK; }
    if (strcmp(s, "warn") == 0)  { *out = BM_LOG_WARN;  return BM_OK; }
    if (strcmp(s, "info") == 0)  { *out = BM_LOG_INFO;  return BM_OK; }
    if (strcmp(s, "debug") == 0) { *out = BM_LOG_DEBUG; return BM_OK; }
    if (strcmp(s, "trace") == 0) { *out = BM_LOG_TRACE; return BM_OK; }
    if (s[0] >= '0' && s[0] <= '4' && s[1] == '\0') {
        *out = (bm_log_level_t)(s[0] - '0');
        return BM_OK;
    }
    return BM_ERR_INVALID;
}

/**
 * @brief "log" 命令：运行期日志级别阈值设置（仅支持 tag="*"，全局阈值）。
 */
static int cmd_log(int argc, char *argv[])
{
    char buf[80];
    bm_log_level_t level;

    if (argc < 3) {
        bm_shell_puts("usage: log <*|tag> <level>\r\n");
        return BM_ERR_INVALID;
    }
    if (strcmp(argv[1], "*") != 0) {
        bm_shell_puts("log: per-tag level not supported yet\r\n");
        return BM_ERR_INVALID;
    }
    if (parse_level(argv[2], &level) != BM_OK) {
        bm_shell_puts("usage: log <*|tag> <error|warn|info|debug|trace|0..4>\r\n");
        return BM_ERR_INVALID;
    }

    bm_log_set_level(level);
    (void)snprintf(buf, sizeof(buf), "log: level=%d (compile ceiling=%d)\r\n",
                   (int)bm_log_get_level(), (int)BM_CONFIG_LOG_LEVEL);
    buf[sizeof(buf) - 1u] = '\0';
    bm_shell_puts(buf);
    return BM_OK;
}

/**
 * @brief "ver" 命令：打印固件编译时间（及可选 git hash）。
 */
static int cmd_ver(int argc, char *argv[])
{
    char buf[64];

    (void)argc;
    (void)argv;

    (void)snprintf(buf, sizeof(buf), "build: %s %s\r\n", __DATE__, __TIME__);
    buf[sizeof(buf) - 1u] = '\0';
    bm_shell_puts(buf);

#ifdef BM_BUILD_GIT_HASH
    (void)snprintf(buf, sizeof(buf), "git: %s\r\n", BM_BUILD_GIT_HASH);
    buf[sizeof(buf) - 1u] = '\0';
    bm_shell_puts(buf);
#endif

    return BM_OK;
}

/**
 * @brief "uptime" 命令：打印开机运行时长（秒.毫秒）。
 */
static int cmd_uptime(int argc, char *argv[])
{
    char buf[48];
    uint64_t us;

    (void)argc;
    (void)argv;

    us = bm_uptime_us();
    (void)snprintf(buf, sizeof(buf), "uptime: %u.%03u s\r\n",
                   (unsigned)(us / 1000000u), (unsigned)((us / 1000u) % 1000u));
    buf[sizeof(buf) - 1u] = '\0';
    bm_shell_puts(buf);
    return BM_OK;
}

int bm_shell_register_builtins(bm_shell_t *shell)
{
    int rc;

    if (shell == NULL) {
        return BM_ERR_INVALID;
    }
    rc = bm_shell_register(shell, "param", cmd_param,
                           "param <list|get|set|save|reset>: 运行期参数表（整定操作台）");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_shell_set_completer(shell, "param", complete_param, NULL);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_shell_register(shell, "log", cmd_log,
                           "log <*|tag> <level>: 运行期日志级别（error..trace 或 0..4）");
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_shell_register(shell, "ver", cmd_ver, "打印固件编译时间");
    if (rc != BM_OK) {
        return rc;
    }
    return bm_shell_register(shell, "uptime", cmd_uptime, "打印开机运行时长");
}

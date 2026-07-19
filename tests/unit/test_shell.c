/**
 * @file test_shell.c
 * @brief Shell 命令注册、解析、feed 与边界条件单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-18
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-07-11       1.1            zeh            shell 交互批①：补模态机制与 Tab 补全用例
 * 2026-07-18       1.2            zeh            shell 交互批③：补参数区补全器用例——
 *                                                唯一匹配/多候选列出/未登记补全器仍响铃
 *                                                （向后兼容基线）/argv_idx+prefix 解析正确性
 */

#include "unity.h"
#include "bm_shell.h"
#include "bm_log.h"

#include <stdio.h>
#include <string.h>

static int g_cmd_count = 0;
static int g_last_argc = 0;
static char *g_last_argv[4] = {0};

int cmd_echo(int argc, char *argv[]) {
    g_cmd_count++;
    g_last_argc = argc;
    for (int i = 0; i < argc && i < 4; i++) {
        g_last_argv[i] = argv[i];
    }
    return BM_OK;
}

int cmd_fail(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return BM_ERR_INVALID;
}

BM_SHELL_DEFINE(my_shell);

/* 模态回调桩计数（定义前置，供 setUp 复位；实现见模态用例节） */
static int g_tick_count;
static int g_stop_count;
static void *g_stop_ctx;

void setUp(void) {
    BM_LOGI("test_shell", "setUp: reset shell state");
    g_cmd_count = 0;
    g_last_argc = 0;
    for (int i = 0; i < 4; i++) g_last_argv[i] = NULL;
    g_tick_count = 0;
    g_stop_count = 0;
    g_stop_ctx = NULL;
    bm_shell_init(&my_shell);
}
void tearDown(void) {}

void test_shell_register_and_exec(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, "echo args"));

    char line[] = "echo hello world";
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&my_shell, line));
    TEST_ASSERT_EQUAL(1, g_cmd_count);
    TEST_ASSERT_EQUAL(3, g_last_argc);
    TEST_ASSERT_EQUAL_STRING("echo", g_last_argv[0]);
    TEST_ASSERT_EQUAL_STRING("hello", g_last_argv[1]);
    TEST_ASSERT_EQUAL_STRING("world", g_last_argv[2]);
}

void test_shell_unknown_command(void) {
    BM_LOGE("test_shell", "expect NOT_FOUND for unknown command");
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND, bm_shell_exec(&my_shell, "noop"));
}

void test_shell_empty_line(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&my_shell, ""));
    TEST_ASSERT_EQUAL(0, g_cmd_count);
}

void test_shell_too_many_cmds(void) {
    for (int i = 0; i < BM_CONFIG_SHELL_MAX_CMDS; i++) {
        char name[8];
        snprintf(name, sizeof(name), "cmd%d", i);
        TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, name, cmd_echo, NULL));
    }
    TEST_ASSERT_EQUAL(BM_ERR_NO_MEM, bm_shell_register(&my_shell, "extra", cmd_echo, NULL));
}

void test_shell_feed_crlf(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 'e');
    bm_shell_feed(&my_shell, 'c');
    bm_shell_feed(&my_shell, 'h');
    bm_shell_feed(&my_shell, 'o');
    bm_shell_feed(&my_shell, ' ');
    bm_shell_feed(&my_shell, '1');
    bm_shell_feed(&my_shell, '\r');
    TEST_ASSERT_EQUAL(1u, my_shell.swallow_lf);
    bm_shell_feed(&my_shell, '\n');

    TEST_ASSERT_EQUAL(1, g_cmd_count);
    TEST_ASSERT_EQUAL(2, g_last_argc);
    TEST_ASSERT_EQUAL_STRING("1", g_last_argv[1]);
    TEST_ASSERT_EQUAL(0u, my_shell.swallow_lf);
}

void test_shell_too_many_args_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, NULL));
    char line[] = "echo a b c d e";
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_shell_exec(&my_shell, line));
}

void test_shell_accepts_exact_argument_limit(void) {
    char line[] = "echo a b c";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&my_shell, line));
    TEST_ASSERT_EQUAL(BM_CONFIG_SHELL_MAX_ARGS, g_last_argc);
}

void test_shell_copies_command_name(void) {
    char name[] = "echo";
    char line[] = "echo";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, name, cmd_echo, NULL));
    name[0] = 'x';
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&my_shell, line));
    TEST_ASSERT_EQUAL(1, g_cmd_count);
}

void test_shell_feed_backspace(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 'e');
    bm_shell_feed(&my_shell, 'x');
    bm_shell_feed(&my_shell, '\b');  /* 退格 */
    bm_shell_feed(&my_shell, 'c');
    bm_shell_feed(&my_shell, 'h');
    bm_shell_feed(&my_shell, 'o');
    bm_shell_feed(&my_shell, '\r');

    TEST_ASSERT_EQUAL(1, g_cmd_count);
    TEST_ASSERT_EQUAL_STRING("echo", g_last_argv[0]);
}

/* =========================================================================
 * 前台模态机制（v1.1）
 * ========================================================================= */

/** 模态 tick 回调桩：只计数。 */
static void modal_tick(void *ctx) {
    (void)ctx;
    g_tick_count++;
}

/** 模态 stop 回调桩：计数并记录 ctx。 */
static void modal_stop(void *ctx) {
    g_stop_count++;
    g_stop_ctx = ctx;
}

void test_shell_modal_enter_invalid_args(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_shell_modal_enter(NULL, modal_tick, modal_stop, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_shell_modal_enter(&my_shell, NULL, modal_stop, NULL));
    TEST_ASSERT_EQUAL(0, bm_shell_modal_active(&my_shell));
}

void test_shell_modal_nested_rejected(void) {
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_shell_modal_enter(&my_shell, modal_tick, modal_stop, NULL));
    TEST_ASSERT_EQUAL(1, bm_shell_modal_active(&my_shell));
    /* 静态单槽：模态中再进入返回 BUSY */
    TEST_ASSERT_EQUAL(BM_ERR_BUSY,
                      bm_shell_modal_enter(&my_shell, modal_tick, modal_stop, NULL));
    /* 清理：Ctrl+C 退出 */
    bm_shell_feed(&my_shell, 0x03);
    TEST_ASSERT_EQUAL(0, bm_shell_modal_active(&my_shell));
}

void test_shell_modal_ctrl_c_calls_stop(void) {
    int marker = 42;

    g_stop_count = 0;
    g_stop_ctx = NULL;
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_shell_modal_enter(&my_shell, modal_tick, modal_stop, &marker));
    bm_shell_feed(&my_shell, 0x03); /* Ctrl+C */
    TEST_ASSERT_EQUAL(1, g_stop_count);
    TEST_ASSERT_EQUAL_PTR(&marker, g_stop_ctx);
    TEST_ASSERT_EQUAL(0, bm_shell_modal_active(&my_shell));
}

void test_shell_modal_discards_other_input(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_shell_modal_enter(&my_shell, modal_tick, NULL, NULL));
    /* 模态期间普通字符与回车均被丢弃：不进行缓冲、不触发命令执行 */
    bm_shell_feed(&my_shell, 'e');
    bm_shell_feed(&my_shell, 'c');
    bm_shell_feed(&my_shell, 'h');
    bm_shell_feed(&my_shell, 'o');
    bm_shell_feed(&my_shell, '\r');
    TEST_ASSERT_EQUAL(0, g_cmd_count);
    TEST_ASSERT_EQUAL(0u, my_shell.cursor);
    TEST_ASSERT_EQUAL(1, bm_shell_modal_active(&my_shell));
    /* stop_fn 为 NULL（只读命令）时 Ctrl+C 亦可正常退出 */
    bm_shell_feed(&my_shell, 0x03);
    TEST_ASSERT_EQUAL(0, bm_shell_modal_active(&my_shell));
}

/* =========================================================================
 * Tab 补全（v1.1）
 * ========================================================================= */

void test_shell_tab_unique_match_completes(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "stats", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "fault", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 's');
    bm_shell_feed(&my_shell, 't');
    bm_shell_feed(&my_shell, '\t'); /* "st" 唯一匹配 stats → 补全 */
    TEST_ASSERT_EQUAL(5u, my_shell.cursor);
    TEST_ASSERT_EQUAL_STRING("stats", my_shell.buf);

    /* 补全后的行可直接回车执行 */
    bm_shell_feed(&my_shell, '\r');
    TEST_ASSERT_EQUAL(1, g_cmd_count);
    TEST_ASSERT_EQUAL_STRING("stats", g_last_argv[0]);
}

void test_shell_tab_multi_match_keeps_prefix(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "stats", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "set", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 's');
    bm_shell_feed(&my_shell, '\t'); /* "s" 匹配 stats/set → 列出候选，前缀保留 */
    TEST_ASSERT_EQUAL(1u, my_shell.cursor);
    TEST_ASSERT_EQUAL('s', my_shell.buf[0]);
}

void test_shell_tab_no_match_keeps_buffer(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "stats", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 'z');
    bm_shell_feed(&my_shell, '\t'); /* 无匹配 → 响铃，缓冲不变 */
    TEST_ASSERT_EQUAL(1u, my_shell.cursor);
    TEST_ASSERT_EQUAL('z', my_shell.buf[0]);
}

void test_shell_tab_completes_builtin_help(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "stats", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 'h');
    bm_shell_feed(&my_shell, '\t'); /* 内建 help 是虚拟候选 → 补全 */
    TEST_ASSERT_EQUAL(4u, my_shell.cursor);
    TEST_ASSERT_EQUAL_STRING("help", my_shell.buf);
}

void test_shell_builtin_help_executes(void) {
    char line[] = "help";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "stats", cmd_echo, "遥测统计"));
    /* 未注册 help 时由框架兜底实现，返回 BM_OK（非 NOT_FOUND） */
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_exec(&my_shell, line));
    TEST_ASSERT_EQUAL(0, g_cmd_count);
}

/* =========================================================================
 * 参数区 Tab 补全（v1.2，功能 A）
 * ========================================================================= */

/** 补全器候选词表：故意含前缀重叠（kp/ki 同 'k' 前缀）覆盖唯一/多候选两支。 */
static void arg_completer_kwlist(uint8_t argv_idx, const char *prefix, uint8_t prefix_len,
                                 bm_shell_complete_emit_fn_t emit, void *emit_ctx,
                                 void *user_ctx) {
    static const char *const k_words[] = { "kp", "ki", "bd" };
    uint8_t i;

    (void)user_ctx;
    if (argv_idx != 1u) return;
    for (i = 0; i < (uint8_t)(sizeof(k_words) / sizeof(k_words[0])); i++) {
        if (strncmp(k_words[i], prefix, (size_t)prefix_len) == 0) {
            emit(emit_ctx, k_words[i]);
        }
    }
}

/** 只记录 (argv_idx, prefix)、不产生候选的探针补全器：专测参数解析正确性。 */
static uint8_t g_probe_argv_idx;
static char    g_probe_prefix[BM_CONFIG_SHELL_MAX_NAME_LEN];

static void arg_completer_probe(uint8_t argv_idx, const char *prefix, uint8_t prefix_len,
                                bm_shell_complete_emit_fn_t emit, void *emit_ctx,
                                void *user_ctx) {
    (void)emit; (void)emit_ctx; (void)user_ctx;
    g_probe_argv_idx = argv_idx;
    memcpy(g_probe_prefix, prefix, prefix_len);
    g_probe_prefix[prefix_len] = '\0';
}

/** 参数区 Tab：命令登记了补全器、唯一匹配 → 自动补全余下字符。 */
void test_shell_arg_completer_unique_match_completes(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "set", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_set_completer(&my_shell, "set", arg_completer_kwlist, NULL));

    bm_shell_feed(&my_shell, 's');
    bm_shell_feed(&my_shell, 'e');
    bm_shell_feed(&my_shell, 't');
    bm_shell_feed(&my_shell, ' ');
    bm_shell_feed(&my_shell, 'b'); /* "b" 在 {kp,ki,bd} 中唯一匹配 bd */
    bm_shell_feed(&my_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("set bd", my_shell.buf);
    TEST_ASSERT_EQUAL(6u, my_shell.cursor);
}

/** 参数区 Tab：多候选 → 缓冲/光标不变（列出候选，不自动补全）。 */
void test_shell_arg_completer_multi_match_keeps_buffer(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "set", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_set_completer(&my_shell, "set", arg_completer_kwlist, NULL));

    bm_shell_feed(&my_shell, 's');
    bm_shell_feed(&my_shell, 'e');
    bm_shell_feed(&my_shell, 't');
    bm_shell_feed(&my_shell, ' ');
    bm_shell_feed(&my_shell, 'k'); /* "k" 同时匹配 kp/ki → 多候选 */
    bm_shell_feed(&my_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("set k", my_shell.buf);
    TEST_ASSERT_EQUAL(5u, my_shell.cursor);
}

/** 参数区 Tab：命令未登记补全器 → 响铃退出，缓冲/光标不变（功能 A 向后
 *  兼容基线：v1.1/v1.2 行为——参数区任何命令 Tab 一律响铃——原样保留）。 */
void test_shell_arg_no_completer_bells_unchanged(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "echo", cmd_echo, NULL));

    bm_shell_feed(&my_shell, 'e');
    bm_shell_feed(&my_shell, 'c');
    bm_shell_feed(&my_shell, 'h');
    bm_shell_feed(&my_shell, 'o');
    bm_shell_feed(&my_shell, ' ');
    bm_shell_feed(&my_shell, 'x');
    bm_shell_feed(&my_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("echo x", my_shell.buf);
    TEST_ASSERT_EQUAL(6u, my_shell.cursor);
}

/** 补全器收到的 argv_idx/prefix 与命令行实际结构一致（多参数、无尾随空格）。 */
void test_shell_arg_completer_argv_idx_and_prefix(void) {
    const char *line = "cmd foo bar";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "cmd", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_set_completer(&my_shell, "cmd", arg_completer_probe, NULL));

    g_probe_argv_idx = 0xFFu;
    g_probe_prefix[0] = '\0';
    for (const char *p = line; *p; p++) bm_shell_feed(&my_shell, *p);
    bm_shell_feed(&my_shell, '\t');

    TEST_ASSERT_EQUAL(2u, g_probe_argv_idx); /* argv[0]=cmd argv[1]=foo argv[2]=bar(当前词) */
    TEST_ASSERT_EQUAL_STRING("bar", g_probe_prefix);
}

/** 补全器收到的 prefix 在尾随空格（当前词为空）时应为空串，argv_idx 仍前移一位。 */
void test_shell_arg_completer_trailing_space_empty_prefix(void) {
    const char *line = "cmd foo ";

    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&my_shell, "cmd", cmd_echo, NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_set_completer(&my_shell, "cmd", arg_completer_probe, NULL));

    g_probe_argv_idx = 0xFFu;
    g_probe_prefix[0] = 'X'; g_probe_prefix[1] = '\0';
    for (const char *p = line; *p; p++) bm_shell_feed(&my_shell, *p);
    bm_shell_feed(&my_shell, '\t');

    TEST_ASSERT_EQUAL(2u, g_probe_argv_idx);
    TEST_ASSERT_EQUAL_STRING("", g_probe_prefix);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_shell_register_and_exec);
    RUN_TEST(test_shell_unknown_command);
    RUN_TEST(test_shell_empty_line);
    RUN_TEST(test_shell_too_many_cmds);
    RUN_TEST(test_shell_feed_crlf);
    RUN_TEST(test_shell_feed_backspace);
    RUN_TEST(test_shell_too_many_args_rejected);
    RUN_TEST(test_shell_accepts_exact_argument_limit);
    RUN_TEST(test_shell_copies_command_name);
    RUN_TEST(test_shell_modal_enter_invalid_args);
    RUN_TEST(test_shell_modal_nested_rejected);
    RUN_TEST(test_shell_modal_ctrl_c_calls_stop);
    RUN_TEST(test_shell_modal_discards_other_input);
    RUN_TEST(test_shell_tab_unique_match_completes);
    RUN_TEST(test_shell_tab_multi_match_keeps_prefix);
    RUN_TEST(test_shell_tab_no_match_keeps_buffer);
    RUN_TEST(test_shell_tab_completes_builtin_help);
    RUN_TEST(test_shell_builtin_help_executes);
    RUN_TEST(test_shell_arg_completer_unique_match_completes);
    RUN_TEST(test_shell_arg_completer_multi_match_keeps_buffer);
    RUN_TEST(test_shell_arg_no_completer_bells_unchanged);
    RUN_TEST(test_shell_arg_completer_argv_idx_and_prefix);
    RUN_TEST(test_shell_arg_completer_trailing_space_empty_prefix);
    return UNITY_END();
}

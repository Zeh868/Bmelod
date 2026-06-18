/**
 * @file test_shell.c
 * @brief Shell 命令注册、解析、feed 与边界条件单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_shell.h"
#include "bm_log.h"

#include <stdio.h>

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

void setUp(void) {
    BM_LOGI("test_shell", "setUp: reset shell state");
    g_cmd_count = 0;
    g_last_argc = 0;
    for (int i = 0; i < 4; i++) g_last_argv[i] = NULL;
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
    return UNITY_END();
}

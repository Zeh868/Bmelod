/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_shell_builtins.c
 * @brief bm_shell_register_builtins 内建命令组单元测试（批 P）
 *
 * 断言基于状态效果与返回码，不抓输出文本——bm_shell_puts 与 shell
 * 同 TU 无法覆写。覆盖 param（get/set/list/save/reset）、log（level
 * 设置）、ver、uptime 四条内建命令经 bm_shell_exec 的行为。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：内建命令组单测）
 *
 */
#include "unity.h"
#include "bm/core/bm_shell.h"
#include "bm/core/bm_shell_builtins.h"
#include "bm/core/bm_param.h"
#include "bm_log.h"

#include <string.h>

static bm_shell_t s_shell;
static float s_kp;

static const bm_param_desc_t k_tbl[] = {
    { "t.kp", 1.0f, &s_kp, NULL, NULL, NULL, 0u },
};

/** @brief 就地可写命令行辅助：bm_shell_exec 会修改缓冲。 */
static int exec_line(const char *cmd) {
    char line[BM_CONFIG_SHELL_BUF_SIZE];

    strncpy(line, cmd, sizeof(line) - 1u);
    line[sizeof(line) - 1u] = '\0';
    return bm_shell_exec(&s_shell, line);
}

void setUp(void) {
    bm_shell_init(&s_shell);
    s_kp = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register_builtins(&s_shell));
    TEST_ASSERT_EQUAL(BM_OK, bm_param_register_table(k_tbl, 1u));
    bm_param_set_reset_guard(NULL);
    bm_log_set_level((bm_log_level_t)BM_CONFIG_LOG_LEVEL);
}
void tearDown(void) {}

void test_builtins_param_set_get_via_shell(void) {
    TEST_ASSERT_EQUAL(BM_OK, exec_line("param set t.kp 2.5"));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, s_kp);
    TEST_ASSERT_EQUAL(BM_OK, exec_line("param get t.kp"));
    TEST_ASSERT_EQUAL(BM_OK, exec_line("param list"));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND, exec_line("param set nope 1"));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, exec_line("param set t.kp abc"));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, exec_line("param bogus"));
}

void test_builtins_param_reset_via_shell(void) {
    TEST_ASSERT_EQUAL(BM_OK, exec_line("param set t.kp 9.0"));
    TEST_ASSERT_EQUAL(BM_OK, exec_line("param reset"));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s_kp);
}

void test_builtins_log_level_via_shell(void) {
    TEST_ASSERT_EQUAL(BM_OK, exec_line("log * warn"));
    TEST_ASSERT_EQUAL((int)BM_LOG_WARN, (int)bm_log_get_level());
    TEST_ASSERT_EQUAL(BM_OK, exec_line("log * 3"));
    TEST_ASSERT_EQUAL((int)BM_LOG_DEBUG, (int)bm_log_get_level());
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, exec_line("log sometag warn"));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, exec_line("log * nope"));
    bm_log_set_level((bm_log_level_t)BM_CONFIG_LOG_LEVEL);
}

void test_builtins_ver_uptime_ok(void) {
    TEST_ASSERT_EQUAL(BM_OK, exec_line("ver"));
    TEST_ASSERT_EQUAL(BM_OK, exec_line("uptime"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_builtins_param_set_get_via_shell);
    RUN_TEST(test_builtins_param_reset_via_shell);
    RUN_TEST(test_builtins_log_level_via_shell);
    RUN_TEST(test_builtins_ver_uptime_ok);
    return UNITY_END();
}

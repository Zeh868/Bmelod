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
 * @version 1.2
 * @date 2026-07-18
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：内建命令组单测）
 * 2026-07-11       1.1            zeh            同步 min/max 新布局；新增 set 越界提示用例
 * 2026-07-18       1.2            zeh            shell 交互批③：新增 param 命令 Tab 补全用例
 *                                                （子命令关键字唯一匹配、参数名唯一匹配，
 *                                                动态取自登记表非写死列表）
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
static float s_bd;

static const bm_param_desc_t k_tbl[] = {
    { "t.kp", 1.0f, 0.0f, 0.0f,  &s_kp, NULL, NULL, NULL, 0u },
    { "t.bd", 1.0f, 0.0f, 10.0f, &s_bd, NULL, NULL, NULL, 0u },
};
#define K_TBL_N ((uint16_t)(sizeof(k_tbl) / sizeof(k_tbl[0])))

/** @brief 就地可写命令行辅助：bm_shell_exec 会修改缓冲。 */
static int exec_line(const char *cmd) {
    char line[BM_CONFIG_SHELL_BUF_SIZE];

    strncpy(line, cmd, sizeof(line) - 1u);
    line[sizeof(line) - 1u] = '\0';
    return bm_shell_exec(&s_shell, line);
}

/** @brief 逐字符喂入字符串（不含 Tab/回车，调用方自行追加）辅助。 */
static void feed_str(const char *s) {
    while (*s) {
        bm_shell_feed(&s_shell, *s);
        s++;
    }
}

void setUp(void) {
    bm_shell_init(&s_shell);
    s_kp = 0.0f;
    s_bd = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register_builtins(&s_shell));
    TEST_ASSERT_EQUAL(BM_OK, bm_param_register_table(k_tbl, K_TBL_N));
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

void test_builtins_param_set_out_of_range(void) {
    TEST_ASSERT_EQUAL(BM_OK, exec_line("param set t.bd 9.5"));
    TEST_ASSERT_EQUAL_FLOAT(9.5f, s_bd);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, exec_line("param set t.bd 99"));
    TEST_ASSERT_EQUAL_FLOAT(9.5f, s_bd); /* 拒绝后落点保持原值（注意 register 不 apply，s_bd 初值非 def） */
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

/* =========================================================================
 * "param" 命令 Tab 补全（v1.2，功能 A）
 * ========================================================================= */

/** "param g<Tab>" → 子命令关键字唯一匹配 "get"（k_sub={list,get,set,save,reset}
 *  中仅 "get" 以 'g' 起首；"s" 前缀会撞 "set"/"save" 两个，故不用它测唯一匹配，
 *  见下一用例）。 */
void test_builtins_param_completes_subcommand_unique(void) {
    feed_str("param g");
    bm_shell_feed(&s_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("param get", s_shell.buf);
    TEST_ASSERT_EQUAL(9u, s_shell.cursor);
}

/** "param s<Tab>" → "set"/"save" 两候选，缓冲/光标不变（多候选列出）。 */
void test_builtins_param_subcommand_multi_match_keeps_buffer(void) {
    feed_str("param s");
    bm_shell_feed(&s_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("param s", s_shell.buf);
    TEST_ASSERT_EQUAL(7u, s_shell.cursor);
}

/** "param set t.k<Tab>" → 参数名唯一匹配 "t.kp"（登记表另一项 "t.bd" 不含此前缀），
 *  动态取自 bm_param_count/bm_param_desc_at，非写死列表。 */
void test_builtins_param_completes_name_for_set(void) {
    feed_str("param set t.k");
    bm_shell_feed(&s_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("param set t.kp", s_shell.buf);
}

/** "param get t.<Tab>" → "t.kp"/"t.bd" 两候选，缓冲/光标不变。 */
void test_builtins_param_name_multi_match_for_get(void) {
    feed_str("param get t.");
    bm_shell_feed(&s_shell, '\t');
    TEST_ASSERT_EQUAL_STRING("param get t.", s_shell.buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_builtins_param_set_get_via_shell);
    RUN_TEST(test_builtins_param_set_out_of_range);
    RUN_TEST(test_builtins_param_reset_via_shell);
    RUN_TEST(test_builtins_log_level_via_shell);
    RUN_TEST(test_builtins_ver_uptime_ok);
    RUN_TEST(test_builtins_param_completes_subcommand_unique);
    RUN_TEST(test_builtins_param_subcommand_multi_match_keeps_buffer);
    RUN_TEST(test_builtins_param_completes_name_for_set);
    RUN_TEST(test_builtins_param_name_multi_match_for_get);
    return UNITY_END();
}

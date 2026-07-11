/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_shell_history.c
 * @brief shell 历史命令与 ESC 序列吞噬单测
 *
 * 覆盖：↑/↓ 整行召回执行、越过最新清空行、连续重复去重、深度回绕、
 * 非 A/B 的 ESC/CSI/SS3 序列静默吞掉（不注入垃圾字符）、召回后继续
 * 编辑复位浏览态。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            初版：bm_shell 历史命令 + ESC 序列吞噬单测
 *
 */
#include "unity.h"
#include "bm/core/bm_shell.h"

#include <string.h>

static bm_shell_t s_shell;
static char s_last_cmd[BM_CONFIG_SHELL_BUF_SIZE];
static int  s_exec_count;

/** @brief 记录命令：把 argv 重组回一行，供召回断言。 */
static int cmd_rec(int argc, char *argv[]) {
    s_last_cmd[0] = '\0';
    for (int i = 0; i < argc; i++) {
        if (i > 0) strcat(s_last_cmd, " ");
        strcat(s_last_cmd, argv[i]);
    }
    s_exec_count++;
    return BM_OK;
}

/** @brief 逐字符喂入字符串。 */
static void feed_str(const char *s) {
    while (*s) bm_shell_feed(&s_shell, *s++);
}

/** @brief 喂入一个 CSI 序列（ESC [ <final>）。 */
static void feed_csi(char final) {
    bm_shell_feed(&s_shell, 0x1B);
    bm_shell_feed(&s_shell, '[');
    bm_shell_feed(&s_shell, final);
}

void setUp(void) {
    bm_shell_init(&s_shell);
    TEST_ASSERT_EQUAL(BM_OK, bm_shell_register(&s_shell, "rec", cmd_rec, NULL));
    s_last_cmd[0] = '\0';
    s_exec_count = 0;
}
void tearDown(void) {}

/** ↑ 召回上一条并可执行。 */
void test_history_up_recalls_and_executes(void) {
    feed_str("rec one\r");
    feed_str("rec two\r");
    TEST_ASSERT_EQUAL(2, s_exec_count);

    feed_csi('A');                       /* ↑ → "rec two" */
    TEST_ASSERT_EQUAL_STRING("rec two", s_shell.buf);
    feed_csi('A');                       /* ↑↑ → "rec one" */
    TEST_ASSERT_EQUAL_STRING("rec one", s_shell.buf);
    feed_str("\r");
    TEST_ASSERT_EQUAL(3, s_exec_count);
    TEST_ASSERT_EQUAL_STRING("rec one", s_last_cmd);
}

/** ↓ 越过最新清空行；空行回车不执行。 */
void test_history_down_past_newest_clears_line(void) {
    feed_str("rec one\r");
    feed_csi('A');
    TEST_ASSERT_EQUAL_STRING("rec one", s_shell.buf);
    feed_csi('B');                       /* ↓ 回到底 → 清空 */
    TEST_ASSERT_EQUAL(0, (int)s_shell.cursor);
    feed_str("\r");
    TEST_ASSERT_EQUAL(1, s_exec_count);  /* 空行不执行 */
}

/** 连续相同命令去重；最旧再 ↑ 停在原地。 */
void test_history_dedup_consecutive(void) {
    feed_str("rec same\r");
    feed_str("rec same\r");
    TEST_ASSERT_EQUAL(1, (int)s_shell.hist_count);
    feed_csi('A');
    feed_csi('A');                       /* 已到最旧：停留 */
    TEST_ASSERT_EQUAL_STRING("rec same", s_shell.buf);
}

/** 深度回绕：写满后挤掉最旧。 */
void test_history_wraps_at_depth(void) {
    char line[16];
    for (int i = 0; i < BM_CONFIG_SHELL_HISTORY_DEPTH + 1; i++) {
        (void)snprintf(line, sizeof(line), "rec n%d\r", i);
        line[sizeof(line) - 1u] = '\0';
        feed_str(line);
    }
    TEST_ASSERT_EQUAL(BM_CONFIG_SHELL_HISTORY_DEPTH, (int)s_shell.hist_count);
    /* 回翻到最旧应为 n1（n0 已被挤掉） */
    for (int i = 0; i < BM_CONFIG_SHELL_HISTORY_DEPTH; i++) feed_csi('A');
    TEST_ASSERT_EQUAL_STRING("rec n1", s_shell.buf);
}

/** 非 A/B 的 CSI 序列被吞：不注入垃圾字符（修现存 bug）。 */
void test_esc_sequences_swallowed(void) {
    bm_shell_feed(&s_shell, 'x');
    feed_csi('C');                       /* → 键 */
    bm_shell_feed(&s_shell, 0x1B);       /* ESC O H（SS3） */
    bm_shell_feed(&s_shell, 'O');
    bm_shell_feed(&s_shell, 'H');
    bm_shell_feed(&s_shell, 'y');
    TEST_ASSERT_EQUAL_STRING("xy", s_shell.buf);
    /* 带参数字节的 CSI（ESC [ 1 ; 5 A）：参数字节整段被吞；本用例历史为空，
     * 终字节 A 仅响铃，行缓冲不被注入任何字符 */
    bm_shell_feed(&s_shell, 0x1B);
    bm_shell_feed(&s_shell, '[');
    bm_shell_feed(&s_shell, '1');
    bm_shell_feed(&s_shell, ';');
    bm_shell_feed(&s_shell, '5');
    bm_shell_feed(&s_shell, 'A');
    TEST_ASSERT_EQUAL_STRING("xy", s_shell.buf);
    feed_str("\r");                       /* 执行 "xy"（unknown 命令，无妨） */
    s_shell.buf[0] = '\0';
}

/** 召回后继续编辑：退格+续输，浏览态复位。 */
void test_recall_then_edit(void) {
    feed_str("rec abc\r");
    feed_csi('A');
    bm_shell_feed(&s_shell, '\b');       /* "rec ab" */
    bm_shell_feed(&s_shell, 'z');        /* "rec abz" */
    TEST_ASSERT_EQUAL_STRING("rec abz", s_shell.buf);
    TEST_ASSERT_EQUAL(0, (int)s_shell.hist_nav);  /* 编辑复位浏览态 */
    feed_str("\r");
    TEST_ASSERT_EQUAL_STRING("rec abz", s_last_cmd);
    /* 历史原条目未被改写 */
    feed_csi('A');
    TEST_ASSERT_EQUAL_STRING("rec abz", s_shell.buf);
    feed_csi('A');
    TEST_ASSERT_EQUAL_STRING("rec abc", s_shell.buf);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_history_up_recalls_and_executes);
    RUN_TEST(test_history_down_past_newest_clears_line);
    RUN_TEST(test_history_dedup_consecutive);
    RUN_TEST(test_history_wraps_at_depth);
    RUN_TEST(test_esc_sequences_swallowed);
    RUN_TEST(test_recall_then_edit);
    return UNITY_END();
}

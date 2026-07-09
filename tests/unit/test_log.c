/**
 * @file test_log.c
 * @brief 日志格式化边界单元测试
 *        （含超长格式串截断后仍输出的边界用例）
 *
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
#include "unity.h"
#include "bm_log.h"

#include <string.h>

static char   g_log_buf[256];
static size_t g_log_len;

void bm_log_output(const char *buf, size_t len) {
    g_log_len = len;
    if (len >= sizeof(g_log_buf)) {
        len = sizeof(g_log_buf) - 1u;
    }
    memcpy(g_log_buf, buf, len);
    g_log_buf[len] = '\0';
}

void setUp(void) {
    g_log_len = 0u;
    memset(g_log_buf, 0, sizeof(g_log_buf));
}
void tearDown(void) {}

void test_log_null_fmt_no_output(void) {
    bm_log(BM_LOG_INFO, "t", NULL);
    TEST_ASSERT_EQUAL(0u, g_log_len);
}

/**
 * @brief 超长格式串应被就地截断到缓冲区上限后仍然输出，而非整条丢弃。
 *
 * bm_log.c 中 vsnprintf 就地截断 buf[BM_CONFIG_LOG_BUF_SIZE]，诊断路径
 * 宁可截断也不丢：本用例断言输出长度落在 (0, BM_CONFIG_LOG_BUF_SIZE) 区间，
 * 而不是旧语义的“超长即整条丢弃（长度为 0）”。
 */
void test_log_truncation_truncates_not_drops(void) {
    char long_fmt[200];

    memset(long_fmt, 'A', sizeof(long_fmt) - 1u);
    long_fmt[sizeof(long_fmt) - 1u] = '\0';

    bm_log(BM_LOG_INFO, "t", long_fmt);
#if BM_CONFIG_LOG_RING
    (void)bm_log_drain_on_this_cpu(1u);
#endif
    TEST_ASSERT_GREATER_THAN(0, (int)g_log_len);
    TEST_ASSERT_LESS_THAN(BM_CONFIG_LOG_BUF_SIZE, (int)g_log_len);
}

void test_log_basic_message(void) {
    bm_log(BM_LOG_INFO, "tag", "hello");
#if BM_CONFIG_LOG_RING
    (void)bm_log_drain_on_this_cpu(1u);
#endif
    TEST_ASSERT_GREATER_THAN(0, (int)g_log_len);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "hello"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_null_fmt_no_output);
    RUN_TEST(test_log_truncation_truncates_not_drops);
    RUN_TEST(test_log_basic_message);
    return UNITY_END();
}

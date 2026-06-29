/**
 * @file test_log_mp.c
 * @brief 多核 per-CPU 日志 ring 单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm_log.h"
#include "hal/bm_hal_cpu.h"
#include "bm_hal_cpu_mp_native.h"

#include <string.h>

static char g_log_buf[256];
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
    bm_hal_cpu_init();
    (void)bm_hal_cpu_native_set_id(0u);
}

void tearDown(void) {
}

void test_log_mp_ring_and_drain(void) {
    TEST_ASSERT_EQUAL(1, bm_log_mp_profile_ok());
#if BM_CONFIG_HARD_RT_PROFILE
    /* hard RT 下 BM_LOG* 整体编译为 no-op（保确定性），日志 ring 不产生条目。 */
    BM_LOGI("mp", "hello0");
    TEST_ASSERT_EQUAL(0u, g_log_len);
    TEST_ASSERT_EQUAL(0u, bm_log_drain_cpu(0u, 4u));
    TEST_ASSERT_EQUAL(0u, g_log_len);
#else
    BM_LOGI("mp", "hello0");
    TEST_ASSERT_EQUAL(0u, g_log_len);
    TEST_ASSERT_EQUAL(1u, bm_log_drain_cpu(0u, 4u));
    TEST_ASSERT_GREATER_THAN(0, (int)g_log_len);
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "hello0"));
#endif
}

void test_log_mp_per_cpu_isolation(void) {
#if BM_CONFIG_CPU_COUNT > 1u && !BM_CONFIG_HARD_RT_PROFILE
    (void)bm_hal_cpu_native_set_id(1u);
    BM_LOGI("mp", "cpu1");
    TEST_ASSERT_EQUAL(0u, g_log_len);
    (void)bm_hal_cpu_native_set_id(0u);
    TEST_ASSERT_EQUAL(0u, bm_log_drain_cpu(0u, 4u));
    TEST_ASSERT_EQUAL(1u, bm_log_drain_cpu(1u, 4u));
    TEST_ASSERT_NOT_NULL(strstr(g_log_buf, "cpu1"));
#else
    /* 单核或 hard RT（日志 no-op）下无 per-CPU ring 隔离语义可验。 */
    TEST_PASS();
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_mp_ring_and_drain);
    RUN_TEST(test_log_mp_per_cpu_isolation);
    return UNITY_END();
}

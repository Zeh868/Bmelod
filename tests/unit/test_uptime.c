/**
 * @file test_uptime.c
 * @brief 单调时钟 bm_uptime_ns() / bm_uptime_us() 单元测试
 *
 * 依赖 native_sim 后端（BM_BACKEND=native_sim），通过
 * bm_hal_uptime_ns_raw() Windows QPC / POSIX CLOCK_MONOTONIC 提供真实时钟。
 *
 * 验证点：
 * 1. bm_uptime_ns() 连续两次调用单调不减；
 * 2. bm_uptime_us() 与 bm_uptime_ns()/1000 量级一致（差距 < 1 s）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #9 时间基统一 1a）
 *
 */
#include "unity.h"
#include "bm_uptime.h"

void setUp(void) {}

void tearDown(void) {}

/**
 * @brief 连续两次调用 bm_uptime_ns() 返回值单调不减
 */
void test_uptime_ns_monotonic(void) {
    uint64_t t0 = bm_uptime_ns();
    uint64_t t1 = bm_uptime_ns();

    TEST_ASSERT_TRUE_MESSAGE(t1 >= t0,
        "bm_uptime_ns() violated monotonicity: t1 < t0");
}

/**
 * @brief bm_uptime_us() 与 bm_uptime_ns()/1000 量级一致
 *
 * 先读 us，再读 ns；由于时间单调递增，ns/1000 >= us。
 * 允许两次读取之间最多 1 s（1e6 us）的流逝（极宽松上界）。
 */
void test_uptime_us_consistent_with_ns(void) {
    uint64_t us = bm_uptime_us();
    uint64_t ns = bm_uptime_ns();
    uint64_t ns_as_us = ns / 1000u;

    /* 时间只会前进：ns_as_us >= us（ns 在 us 之后读取）*/
    TEST_ASSERT_TRUE_MESSAGE(ns_as_us >= us,
        "bm_uptime_us() > bm_uptime_ns()/1000: clock not consistent");

    /* 两次读取间隔 < 1 s（测试机极慢时仍可通过）*/
    TEST_ASSERT_TRUE_MESSAGE((ns_as_us - us) < 1000000u,
        "gap between bm_uptime_us() and bm_uptime_ns()/1000 exceeds 1 s");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_uptime_ns_monotonic);
    RUN_TEST(test_uptime_us_consistent_with_ns);
    return UNITY_END();
}

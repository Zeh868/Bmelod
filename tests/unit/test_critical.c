/**
 * @file test_critical.c
 * @brief 原子变量饱和与空指针单元测试
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
#include "bm_atomic.h"
#include "bm_safety.h"

void setUp(void) {}
void tearDown(void) {}

void test_atomic_inc_saturates_at_max(void) {
    bm_atomic_t v = UINT32_MAX;

    TEST_ASSERT_EQUAL(UINT32_MAX, bm_atomic_inc(&v));
    TEST_ASSERT_EQUAL(UINT32_MAX, bm_atomic_load(&v));
}

void test_atomic_load_null_returns_zero(void) {
    TEST_ASSERT_EQUAL(0u, bm_atomic_load(NULL));
}

void test_atomic_inc_null_returns_zero(void) {
    TEST_ASSERT_EQUAL(0u, bm_atomic_inc(NULL));
}

void test_safety_diagnostic_counters_saturate(void) {
    TEST_ASSERT_EQUAL(UINT32_MAX, bm_u32_saturating_inc(UINT32_MAX));
    TEST_ASSERT_EQUAL(UINT32_MAX,
                      bm_u32_saturating_add(UINT32_MAX - 1u, 2u));
    TEST_ASSERT_EQUAL(3u, bm_u32_saturating_add(1u, 2u));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_atomic_inc_saturates_at_max);
    RUN_TEST(test_atomic_load_null_returns_zero);
    RUN_TEST(test_atomic_inc_null_returns_zero);
    RUN_TEST(test_safety_diagnostic_counters_saturate);
    return UNITY_END();
}

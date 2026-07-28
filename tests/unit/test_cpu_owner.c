/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_cpu_owner.c
 * @brief bm_cpu_is_owner() 统一 owner 守卫原语单元测试
 *
 * 覆盖：
 *   - 单核 no-op：任意 owner 值（含越界）恒返回 1；
 *   - BM_CPU_ANY（0xFFu）：任意核编译配置下恒真；
 *   - 多核路由路径：当前 CPU owner 为真，其他合法核及越界 owner 为假。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-27       1.0            zeh            初始 owner 守卫测试
 * 2026-07-28       1.1            zeh            按路由配置分别验证单核与 SMP 语义
 */
#include "unity.h"
#include "bm/core/bm_cpu_local.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief BM_CPU_ANY（0xFFu）：多核/单核均恒真
 *
 * 无论 BM_CPU_LOCAL_ENABLE_ROUTE 为 0 还是 1，BM_CPU_ANY 表示任意核，守卫恒真。
 */
void test_is_owner_cpu_any_always_true(void) {
    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner((uint8_t)BM_CPU_ANY));
    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner(0xFFu));
}

/**
 * @brief 按编译期路由配置验证 owner 守卫语义
 *
 * 单核 no-op 路径对任意 owner 恒真；SMP 路径要求当前 CPU 为 owner，
 * 同时拒绝另一个合法 CPU 与越界 owner。
 */
void test_is_owner_configuration_semantics(void) {
#if BM_CPU_LOCAL_ENABLE_ROUTE
    uint8_t current = (uint8_t)BM_CPU_THIS();
    uint8_t other =
        (uint8_t)(((uint32_t)current + 1u) % (uint32_t)BM_CONFIG_CPU_COUNT);
    uint8_t invalid = (uint8_t)BM_CONFIG_CPU_COUNT;

    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner((uint8_t)BM_CPU_ANY));
    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner(current));
    TEST_ASSERT_EQUAL_INT(0, bm_cpu_is_owner(other));
    TEST_ASSERT_EQUAL_INT(0, bm_cpu_is_owner(invalid));
#else
    uint8_t values[] = { 0u, 1u, 2u, 0xFEu, (uint8_t)BM_CPU_ANY };
    uint32_t i;

    for (i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
        TEST_ASSERT_NOT_EQUAL(0, bm_cpu_is_owner(values[i]));
    }
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_is_owner_cpu_any_always_true);
    RUN_TEST(test_is_owner_configuration_semantics);
    return UNITY_END();
}

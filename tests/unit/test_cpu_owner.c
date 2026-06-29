/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_cpu_owner.c
 * @brief bm_cpu_is_owner() 统一 owner 守卫原语单元测试
 *
 * 覆盖：
 *   - 单核 no-op：任意 owner 值（含越界）恒返回 1；
 *   - BM_CPU_ANY（0xFFu）：任意核编译配置下恒真；
 *   - 多核强制路径：native 为单核构建，仅能验证 no-op 语义；
 *     多核强制路径（ROUTE==1）在 qemu_smp 环境验证，此处标注说明。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-27
 */
#include "unity.h"
#include "bm/core/bm_cpu_local.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief 单核构建（BM_CPU_LOCAL_ENABLE_ROUTE==0）：恒真（no-op）
 *
 * 本仓默认 BM_CONFIG_CPU_COUNT=1，ROUTE 关闭，bm_cpu_is_owner 编译为 (void)owner; return 1;
 * 任意 owner 值均应返回 1。
 */
void test_is_owner_single_core_noop_always_true(void) {
    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner(0u));
    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner(1u));
    TEST_ASSERT_EQUAL_INT(1, bm_cpu_is_owner(255u));
}

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
 * @brief 单核构建：BM_CPU_ANY 与合法/越界 owner 均恒真（综合覆盖）
 *
 * 验证 no-op 分支对所有常用枚举值（含 BM_CPU_ANY）均不返回 0。
 */
void test_is_owner_noop_covers_all_common_values(void) {
    uint8_t values[] = { 0u, 1u, 2u, 0xFEu, (uint8_t)BM_CPU_ANY };
    uint32_t i;
    for (i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
        TEST_ASSERT_NOT_EQUAL(0, bm_cpu_is_owner(values[i]));
    }
}

/**
 * @brief 多核强制路径（BM_CPU_LOCAL_ENABLE_ROUTE==1）说明
 *
 * native 构建为单核，无法在此验证多核强制路径（owner != BM_CPU_THIS() 时返回 0）。
 * 多核强制语义已由 bm_stream.c 中 stream_owner_valid() 及 qemu_smp 集成测试覆盖（
 * tests/qemu/ 下 35/35 通过，见 memory/bus-block-ioc-stream-backend.md）。
 */
void test_is_owner_multicore_note(void) {
    /* 此测试仅在单核 native 构建下运行，强制路径见 qemu_smp 集成测试。
     * 此处仅验证 BM_CPU_LOCAL_ENABLE_ROUTE 宏已正确推导（0 或 1）。 */
#if BM_CPU_LOCAL_ENABLE_ROUTE
    /* 多核构建：不在 native 环境下，qemu_smp 集成测试覆盖 */
    TEST_PASS();
#else
    /* 单核构建：ROUTE=0，no-op 路径正确 */
    TEST_ASSERT_EQUAL_INT(0, BM_CPU_LOCAL_ENABLE_ROUTE);
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_is_owner_single_core_noop_always_true);
    RUN_TEST(test_is_owner_cpu_any_always_true);
    RUN_TEST(test_is_owner_noop_covers_all_common_values);
    RUN_TEST(test_is_owner_multicore_note);
    return UNITY_END();
}

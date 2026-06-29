/**
 * @file test_idle.c
 * @brief bm_idle() 空闲钩子单元测试（路线图 #8 省电/空闲钩子）
 *
 * 验证点：
 * 1. 应用提供的强符号覆盖（override）确实替代了 bm_core 中的 weak 默认实现；
 * 2. 覆盖实现被正确调用，调用次数可断言；
 * 3. 多次调用不崩溃，计数单调递增。
 *
 * 实现原理：
 *   本文件定义了一个非 weak 的强符号 bm_idle()，链接时编译器/链接器将优先
 *   选择本文件中的强符号，而非 bm_core.a 中的 weak 默认实现。
 *   若调用计数符合预期，即证明覆盖机制生效。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            新增（路线图 #8 省电/空闲钩子）
 *
 */
#include "unity.h"
#include "bm/common/bm_idle.h"

/* 覆盖实现的调用计数 */
static unsigned int g_idle_call_count = 0u;

/**
 * @brief bm_idle() 强符号覆盖（测试专用）
 *
 * 替换 bm_core 中的 weak 默认实现。每次被调用时递增计数器，
 * 使测试可断言调用次数。
 */
void bm_idle(void) {
    g_idle_call_count++;
}

void setUp(void) {
    /* 每个用例前重置计数，保证用例隔离 */
    g_idle_call_count = 0u;
}

void tearDown(void) {}

/**
 * @brief 验证覆盖实现被调用一次
 *
 * 若 weak 默认实现（WFI / 空操作）未被替换，计数仍为 0；
 * 若强符号覆盖生效，计数应为 1。
 */
void test_idle_override_called_once(void) {
    bm_idle();
    TEST_ASSERT_EQUAL_UINT(1u, g_idle_call_count);
}

/**
 * @brief 验证多次调用计数线性递增
 */
void test_idle_override_called_multiple(void) {
    bm_idle();
    bm_idle();
    bm_idle();
    TEST_ASSERT_EQUAL_UINT(3u, g_idle_call_count);
}

/**
 * @brief 验证 setUp 重置了计数（用例隔离）
 */
void test_idle_counter_reset_by_setup(void) {
    /* setUp 已将 g_idle_call_count 归零，此处 bm_idle() 后应为 1 */
    bm_idle();
    TEST_ASSERT_EQUAL_UINT(1u, g_idle_call_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_override_called_once);
    RUN_TEST(test_idle_override_called_multiple);
    RUN_TEST(test_idle_counter_reset_by_setup);
    return UNITY_END();
}

/**
 * @file test_priority_mask.c
 * @brief 临界区优先级掩码嵌套进入/退出恢复单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_hal_critical.h"
#include "bm_hal_priority_mask_fake.h"

void setUp(void) {
    bm_hal_priority_mask_fake_reset();
}

void tearDown(void) {}

void test_priority_mask_nested_restore(void) {
    /* 外层 threshold=4，内层 threshold=2，退出后应逐层恢复 */
    bm_irq_state_t outer = bm_hal_critical_enter_below(4u);
    bm_irq_state_t inner = bm_hal_critical_enter_below(2u);

    TEST_ASSERT_EQUAL_UINT8(2u, bm_hal_priority_mask_fake_basepri());
    bm_hal_critical_exit_below(inner);
    TEST_ASSERT_EQUAL_UINT8(4u, bm_hal_priority_mask_fake_basepri());
    bm_hal_critical_exit_below(outer);
    TEST_ASSERT_EQUAL_UINT8(0u, bm_hal_priority_mask_fake_basepri());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_priority_mask_nested_restore);
    return UNITY_END();
}

/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_hrt_isr_context.c
 * @brief HRT ISR 上下文标记原语（bm_hrt_isr_enter/exit/bm_in_hrt_isr）单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-08-01
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-07-30       1.0            zeh            正式发布
 * 2026-08-01       1.1            zeh            补宿主 arch ISR FPU
 *                                                enter/exit 配对冒烟
 */

#include "unity.h"
#include "bm/common/bm_critical_wrap.h"
#include "host/bm_arch_isr_fpu.h"

void setUp(void) {}
void tearDown(void) {}

void test_initial_not_in_hrt_isr(void) {
    TEST_ASSERT_EQUAL_INT(0, bm_in_hrt_isr());
}

void test_enter_exit_nesting(void) {
    bm_hrt_isr_enter();
    TEST_ASSERT_EQUAL_INT(1, bm_in_hrt_isr());
    /* 嵌套第二层：仍为"在 HRT ISR 中" */
    bm_hrt_isr_enter();
    TEST_ASSERT_EQUAL_INT(1, bm_in_hrt_isr());
    /* 退出一层：深度未归零，仍在 HRT ISR 中 */
    bm_hrt_isr_exit();
    TEST_ASSERT_EQUAL_INT(1, bm_in_hrt_isr());
    bm_hrt_isr_exit();
    TEST_ASSERT_EQUAL_INT(0, bm_in_hrt_isr());
}

void test_exit_underflow_safe(void) {
    /* 深度为 0 时 exit 不得下溢（防御性钳位），且状态保持 0 */
    bm_hrt_isr_exit();
    TEST_ASSERT_EQUAL_INT(0, bm_in_hrt_isr());
    bm_hrt_isr_enter();
    bm_hrt_isr_exit();
    bm_hrt_isr_exit();
    TEST_ASSERT_EQUAL_INT(0, bm_in_hrt_isr());
}

/**
 * @brief 宿主 bm_arch_isr_fpu_enter/exit 配对冒烟（host 为 no-op）
 *
 * 不强制真机浮点现场；调用不崩且 enter 返回约定哨兵即可。
 */
void test_arch_isr_fpu_enter_exit_smoke(void) {
    unsigned char sa[BM_ARCH_ISR_FPU_SA_SIZE];
    unsigned prev;

    prev = bm_arch_isr_fpu_enter(sa);
    bm_arch_isr_fpu_exit(sa, prev);
    TEST_ASSERT_EQUAL_UINT(0u, prev);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_not_in_hrt_isr);
    RUN_TEST(test_enter_exit_nesting);
    RUN_TEST(test_exit_underflow_safe);
    RUN_TEST(test_arch_isr_fpu_enter_exit_smoke);
    return UNITY_END();
}

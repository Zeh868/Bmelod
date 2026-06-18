/**
 * @file test_arch_port.c
 * @brief 架构层临界区烟雾测试（宿主 PC，链接 arch/stub）
 *
 * 验证 bm_drv_critical_api enter/exit 嵌套与 portmacro 可编译链接。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#define BM_DRV_CRITICAL_API
#include "bm_drv_critical.h"
#include "stub/bm_arch_portmacro.h"

void setUp(void) {}
void tearDown(void) {}

void test_arch_critical_nested_enter_exit(void) {
    bm_irq_state_t outer;
    bm_irq_state_t inner;

    outer = bm_drv_critical_api.enter();
    inner = bm_drv_critical_api.enter();
    bm_drv_critical_api.exit(inner);
    bm_drv_critical_api.exit(outer);
    TEST_ASSERT_EQUAL(0, bm_drv_critical_api.in_isr());
}

void test_arch_portmacro_dmb_yield_compile(void) {
    BM_ARCH_DMB();
    BM_ARCH_YIELD();
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_arch_critical_nested_enter_exit);
    RUN_TEST(test_arch_portmacro_dmb_yield_compile);
    return UNITY_END();
}

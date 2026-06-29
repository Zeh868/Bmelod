/**
 * @file test_mp_profile_epoch.c
 * @brief profile 代际查询注册表（bm_profile_epoch_*）单元测试
 *
 * 覆盖：未注册时 current 返回 0；注册查询函数后 current 透传其返回值；
 * 首次注册后以不同指针再注册被忽略（注册即冻结）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm/common/bm_profile_epoch.h"
#include "hal/bm_hal_cpu.h"
#include "bm_hal_cpu_mp_native.h"

#include <stdint.h>

static uint32_t epoch_returns_7(void) {
    return 7u;
}

static uint32_t epoch_returns_9(void) {
    return 9u;
}

void setUp(void) {
    bm_hal_cpu_init();
    (void)bm_hal_cpu_native_set_id(0u);
}

void tearDown(void) {
}

void test_profile_epoch_register_and_query(void) {
    /* 注册前：返回 0。 */
    TEST_ASSERT_EQUAL_UINT32(0u, bm_profile_epoch_current());

    /* 注册后：透传查询函数返回值。 */
    bm_profile_epoch_register(epoch_returns_7);
    TEST_ASSERT_EQUAL_UINT32(7u, bm_profile_epoch_current());

    /* 注册即冻结：以不同指针再注册被忽略。 */
    bm_profile_epoch_register(epoch_returns_9);
    TEST_ASSERT_EQUAL_UINT32(7u, bm_profile_epoch_current());

    /* 以相同指针再注册：幂等。 */
    bm_profile_epoch_register(epoch_returns_7);
    TEST_ASSERT_EQUAL_UINT32(7u, bm_profile_epoch_current());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_profile_epoch_register_and_query);
    return UNITY_END();
}

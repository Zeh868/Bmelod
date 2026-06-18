/**
 * @file test_skeleton.c
 * @brief Unity 测试骨架：验证测试框架与构建链路可用
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_log.h"

void setUp(void) {
    BM_LOGI("test_skel", "setUp");
}
void tearDown(void) {}

void test_skeleton_passes(void) {
    TEST_ASSERT_EQUAL(1, 1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_skeleton_passes);
    return UNITY_END();
}

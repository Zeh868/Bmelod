/**
 * @file test_motion_profile.c
 * @brief motion_profile 组件单元测试
 *
 * 覆盖梯形与 S 曲线 goto、step 输出位置与速度。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 */
#include "unity.h"
#include "bm/component/motion_profile.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_motion_profile_trapezoid_reaches_target(void) {
    bm_motion_profile_axis_t axis;
    bm_motion_profile_output_t out;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_TRAP;
    axis.config.vmax = 1.0f;
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_profile_validate_config(&axis.config));
    bm_motion_profile_reset(&axis, 0.0f);
    bm_motion_profile_goto(&axis, 1.0f);

    for (i = 0u; i < 500u; ++i) {
        bm_motion_profile_step(&axis, &out);
        if (out.done) {
            break;
        }
    }

    TEST_ASSERT_TRUE(out.done != 0);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, out.position);
}

void test_motion_profile_scurve_moves(void) {
    bm_motion_profile_axis_t axis;
    bm_motion_profile_output_t out;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_SCURVE;
    axis.config.jerk = 5.0f;
    axis.config.vmax = 1.0f;
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;
    bm_motion_profile_reset(&axis, 0.0f);
    bm_motion_profile_goto(&axis, 0.5f);

    for (i = 0u; i < 300u; ++i) {
        bm_motion_profile_step(&axis, &out);
    }

    TEST_ASSERT_TRUE(out.position > 0.1f);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motion_profile_trapezoid_reaches_target);
    RUN_TEST(test_motion_profile_scurve_moves);
    return UNITY_END();
}

/**
 * @file test_additive_motion.c
 * @brief additive_motion 组件单元测试
 *
 * 覆盖 ZV 输入整形阶跃响应基本行为。
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
#include "bm/component/additive_motion.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_additive_zv_shapes_step(void) {
    bm_additive_motion_axis_t axis;
    int i;

    memset(&axis, 0, sizeof(axis));
    axis.config.natural_freq_hz = 10.0f;
    axis.config.damping_ratio = 0.05f;
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 100.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_init(&axis));
    bm_additive_motion_shape_cmd(&axis, 10.0f);
    TEST_ASSERT_TRUE(axis.state.shaped_mm > 0.0f);
    TEST_ASSERT_TRUE(axis.state.shaped_mm < 10.0f);

    for (i = 0; i < 200; ++i) {
        bm_additive_motion_shape_cmd(&axis, 10.0f);
        bm_additive_motion_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, axis.state.shaped_mm);
}

void test_additive_pressure_advance_linear(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f,
        bm_additive_motion_pressure_advance(5.0f, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f,
        bm_additive_motion_pressure_advance(-2.0f, 0.5f));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_additive_zv_shapes_step);
    RUN_TEST(test_additive_pressure_advance_linear);
    return UNITY_END();
}

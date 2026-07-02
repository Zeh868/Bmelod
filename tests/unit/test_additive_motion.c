/**
 * @file test_additive_motion.c
 * @brief additive_motion 组件单元测试
 *
 * 覆盖 ZV 输入整形阶跃响应、delay_steps 超 buffer 边界钳位、
 * velocity 饱和限幅、ZV 系数正确性及 exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补 buffer 边界/velocity 饱和/ZV 系数/exec_ops 测试
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/additive_motion.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <stddef.h>
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

/* delay_steps 超 buffer 上限时被钳位至 BM_ADDITIVE_ZV_BUFFER_MAX-1 */
void test_additive_delay_steps_clamped(void) {
    bm_additive_motion_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    /* 极低频率 → td 极大 → steps 超 buffer */
    axis.config.natural_freq_hz = 0.001f; /* td = π/0.001 ≈ 3141 s */
    axis.config.damping_ratio = 0.05f;
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 100.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_init(&axis));
    TEST_ASSERT_EQUAL(BM_ADDITIVE_ZV_BUFFER_MAX - 1u,
                      axis.state.delay_steps);
    TEST_ASSERT_EQUAL(BM_ADDITIVE_ZV_BUFFER_MAX,
                      axis.state.buffer_len);
}

/* velocity 饱和：step 后 telemetry.velocity_mm_s 不超过 max_velocity_mm_s */
void test_additive_velocity_saturation(void) {
    bm_additive_motion_axis_t axis;
    int i;

    memset(&axis, 0, sizeof(axis));
    axis.config.natural_freq_hz = 10.0f;
    axis.config.damping_ratio = 0.05f;
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 5.0f; /* 较小上限 */

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_init(&axis));

    /* 施加大幅阶跃以触发速度饱和 */
    for (i = 0; i < 10; i++) {
        bm_additive_motion_shape_cmd(&axis, 1000.0f);
        bm_additive_motion_step(&axis);
        TEST_ASSERT_TRUE(
            fabsf(axis.state.telemetry.velocity_mm_s) <=
            axis.config.max_velocity_mm_s + 0.001f);
    }
}

/* ZV 系数正确性：a0+a1 应恒等于 1.0 */
void test_additive_zv_coeffs_sum_to_one(void) {
    bm_additive_motion_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.natural_freq_hz = 20.0f;
    axis.config.damping_ratio = 0.1f;
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 100.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_init(&axis));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f,
                             axis.state.a0 + axis.state.a1);
}

/*
 * P0-5b：速度遥测不应恒为 0。整形位置在 shape_cmd 后发生变化，step 应据跨周期
 * 差分算出非零速度（此前 prev 在同一 step 内取快照使 vel 恒为 0）。
 */
void test_additive_velocity_nonzero_after_shape(void) {
    bm_additive_motion_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.natural_freq_hz = 10.0f;
    axis.config.damping_ratio = 0.05f;
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 1.0e9f; /* 极大上限，避免饱和掩盖差分 */

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_init(&axis));

    /* 施加阶跃使 shaped_mm 变为正值 */
    bm_additive_motion_shape_cmd(&axis, 10.0f);
    TEST_ASSERT_TRUE(axis.state.shaped_mm > 0.0f);

    /* step 应算出非零速度（shaped_mm 相对上一周期 0 的差分 / dt） */
    bm_additive_motion_step(&axis);
    TEST_ASSERT_TRUE(fabsf(axis.state.telemetry.velocity_mm_s) > 0.0f);

    /* 位置稳定（无新指令）后，下一 step 速度应回落到 0 */
    bm_additive_motion_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.telemetry.velocity_mm_s);
}

/* P0-5b：阻尼比 ≥ 1 时系数须有限（不产生 NaN） */
void test_additive_overdamped_coeffs_finite(void) {
    bm_additive_motion_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.natural_freq_hz = 10.0f;
    axis.config.damping_ratio = 1.5f; /* 过阻尼，触发 sqrt(1-ζ²) 保护 */
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 100.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_init(&axis));
    /* a0/a1 须为有限值且和为 1（NaN != NaN，故等式检查同时排除 NaN） */
    TEST_ASSERT_TRUE(axis.state.a0 == axis.state.a0);
    TEST_ASSERT_TRUE(axis.state.a1 == axis.state.a1);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, axis.state.a0 + axis.state.a1);
}

/* exec_ops：init → start → safe_stop 生命周期测试 */
void test_additive_exec_ops_lifecycle(void) {
    static bm_additive_motion_axis_t axis;
    bm_exec_t inst;

    memset(&axis, 0, sizeof(axis));
    axis.config.natural_freq_hz = 10.0f;
    axis.config.damping_ratio = 0.05f;
    axis.config.dt_s = 0.001f;
    axis.config.max_velocity_mm_s = 100.0f;

    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_exec_init(&inst));
    TEST_ASSERT_EQUAL(BM_OK, bm_additive_motion_exec_start(&inst));

    bm_additive_motion_shape_cmd(&axis, 50.0f);
    bm_additive_motion_exec_safe_stop(&inst);

    /* safe_stop 应将整形输出清零 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.shaped_mm);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.last_cmd_mm);
}

/* exec_ops：NULL 实例不崩溃 */
void test_additive_exec_ops_null_safe(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_additive_motion_exec_init(NULL));
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_additive_motion_exec_start(NULL));
    bm_additive_motion_exec_safe_stop(NULL); /* 不崩溃即通过 */
    bm_additive_motion_exec_run(NULL);       /* 不崩溃即通过 */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_additive_zv_shapes_step);
    RUN_TEST(test_additive_pressure_advance_linear);
    RUN_TEST(test_additive_delay_steps_clamped);
    RUN_TEST(test_additive_velocity_saturation);
    RUN_TEST(test_additive_zv_coeffs_sum_to_one);
    RUN_TEST(test_additive_velocity_nonzero_after_shape);
    RUN_TEST(test_additive_overdamped_coeffs_finite);
    RUN_TEST(test_additive_exec_ops_lifecycle);
    RUN_TEST(test_additive_exec_ops_null_safe);
    return UNITY_END();
}

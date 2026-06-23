/**
 * @file test_motion_profile.c
 * @brief motion_profile 组件单元测试
 *
 * 覆盖梯形与 S 曲线 goto/step 正常路径、边界（target==current 即时到达）、
 * S 曲线到达判定、NULL 安全、exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补边界：target==current、S曲线到达、NULL安全、exec_ops
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/motion_profile.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ================================================================
 * 测试 1：梯形轮廓能到达目标（既有，保留）
 * ================================================================ */
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

/* ================================================================
 * 测试 2：S 曲线轮廓能移动（既有，保留）
 * ================================================================ */
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

/* ================================================================
 * 测试 3：target == current（梯形）——首拍即应返回 done=1
 *
 * 不调用 goto，轴 reset 后 active=0，step 直接输出当前位置且 done=1。
 * ================================================================ */
void test_motion_profile_trapezoid_target_equals_current_is_done(void) {
    bm_motion_profile_axis_t axis;
    bm_motion_profile_output_t out;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_TRAP;
    axis.config.vmax = 1.0f;
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;

    bm_motion_profile_reset(&axis, 5.0f);
    /* 不 goto：active 仍为 0，表示已在目标位置 */
    bm_motion_profile_step(&axis, &out);

    TEST_ASSERT_EQUAL(1, out.done);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, out.position);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out.velocity);
}

/* ================================================================
 * 测试 4：S 曲线到达目标后 done=1，位置在目标附近
 * ================================================================ */
void test_motion_profile_scurve_reaches_target_done(void) {
    bm_motion_profile_axis_t axis;
    bm_motion_profile_output_t out;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_SCURVE;
    axis.config.jerk = 10.0f;
    axis.config.vmax = 1.0f;
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;
    bm_motion_profile_reset(&axis, 0.0f);
    bm_motion_profile_goto(&axis, 0.5f);

    out.done = 0;
    for (i = 0u; i < 1000u; ++i) {
        bm_motion_profile_step(&axis, &out);
        if (out.done) {
            break;
        }
    }

    TEST_ASSERT_EQUAL(1, out.done);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, out.position);
}

/* ================================================================
 * 测试 5：NULL 安全——axis=NULL 时 step 不崩溃
 * ================================================================ */
void test_motion_profile_step_null_axis_safe(void) {
    bm_motion_profile_output_t out;

    memset(&out, 0, sizeof(out));
    /* 不应崩溃，也不应修改 out */
    bm_motion_profile_step(NULL, &out);
    TEST_ASSERT_EQUAL(0, out.done);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, out.position);
}

/* ================================================================
 * 测试 6：NULL 安全——out=NULL 时 step 不崩溃
 * ================================================================ */
void test_motion_profile_step_null_out_safe(void) {
    bm_motion_profile_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_TRAP;
    axis.config.vmax = 1.0f;
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;
    bm_motion_profile_reset(&axis, 0.0f);

    /* 不应崩溃 */
    bm_motion_profile_step(&axis, NULL);
}

/* ================================================================
 * 测试 7：NULL 安全——goto(NULL) 不崩溃
 * ================================================================ */
void test_motion_profile_goto_null_safe(void) {
    /* 不应崩溃 */
    bm_motion_profile_goto(NULL, 1.0f);
}

/* ================================================================
 * 测试 8：validate_config NULL 返回 BM_ERR_INVALID
 * ================================================================ */
void test_motion_profile_validate_null_returns_invalid(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_motion_profile_validate_config(NULL));
}

/* ================================================================
 * 测试 9：validate_config 拒绝 dt_s <= 0
 * ================================================================ */
void test_motion_profile_validate_rejects_zero_dt(void) {
    bm_motion_profile_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.type = BM_MOTION_PROFILE_TRAP;
    cfg.vmax = 1.0f;
    cfg.amax = 2.0f;
    cfg.dt_s = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_motion_profile_validate_config(&cfg));
}

/* ================================================================
 * 测试 10：exec_ops 生命周期
 * ================================================================ */
void test_motion_profile_exec_ops_lifecycle(void) {
    bm_motion_profile_axis_t axis;
    bm_exec_t                exec;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_TRAP;
    axis.config.vmax = 1.0f;
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;

    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    /* init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_profile_exec_ops.init(&exec));
    /* start 应返回 BM_OK */
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_profile_exec_ops.start(&exec));

    /* safe_stop 应清除 active */
    bm_motion_profile_goto(&axis, 1.0f);
    TEST_ASSERT_EQUAL(1, axis.state.active);
    bm_motion_profile_exec_ops.safe_stop(&exec);
    TEST_ASSERT_EQUAL(0, axis.state.active);
}

/* ================================================================
 * 测试 11：exec_ops init 对非法配置返回 BM_ERR_INVALID
 * ================================================================ */
void test_motion_profile_exec_ops_init_rejects_bad_config(void) {
    bm_motion_profile_axis_t axis;
    bm_exec_t                exec;

    memset(&axis, 0, sizeof(axis));
    axis.config.type = BM_MOTION_PROFILE_TRAP;
    axis.config.vmax = 0.0f; /* 非法 */
    axis.config.amax = 2.0f;
    axis.config.dt_s = 0.01f;

    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_motion_profile_exec_ops.init(&exec));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motion_profile_trapezoid_reaches_target);
    RUN_TEST(test_motion_profile_scurve_moves);
    RUN_TEST(test_motion_profile_trapezoid_target_equals_current_is_done);
    RUN_TEST(test_motion_profile_scurve_reaches_target_done);
    RUN_TEST(test_motion_profile_step_null_axis_safe);
    RUN_TEST(test_motion_profile_step_null_out_safe);
    RUN_TEST(test_motion_profile_goto_null_safe);
    RUN_TEST(test_motion_profile_validate_null_returns_invalid);
    RUN_TEST(test_motion_profile_validate_rejects_zero_dt);
    RUN_TEST(test_motion_profile_exec_ops_lifecycle);
    RUN_TEST(test_motion_profile_exec_ops_init_rejects_bad_config);
    return UNITY_END();
}

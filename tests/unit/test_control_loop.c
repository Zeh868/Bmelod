/**
 * @file test_control_loop.c
 * @brief control_loop 串级 PI 组件单元测试
 *
 * 覆盖外环设定、内环跟踪与输出饱和基本行为；
 * 并覆盖 exec 生命周期（init → start → step → safe_stop）路径。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补 exec 生命周期测试
 */
#include "unity.h"
#include "bm/component/control_loop.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static float g_outer_meas;
static float g_inner_meas;
static float g_setpoint;
static float g_last_output;

static int read_plant(void *user, float *outer_meas, float *inner_meas,
                      float *setpoint) {
    (void)user;
    *outer_meas = g_outer_meas;
    *inner_meas = g_inner_meas;
    *setpoint = g_setpoint;
    return 0;
}

static int write_output(void *user, float output) {
    (void)user;
    g_last_output = output;
    g_inner_meas += (output - g_inner_meas) * 0.2f;
    g_outer_meas += (g_inner_meas - g_outer_meas) * 0.1f;
    return 0;
}

void setUp(void) {
    g_outer_meas = 0.0f;
    g_inner_meas = 0.0f;
    g_setpoint = 1.0f;
    g_last_output = 0.0f;
}

void tearDown(void) {
}

static void test_validate_config_rejects_bad_dt(void) {
    bm_control_loop_config_t cfg = { .dt_s = 0.0f };
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_control_loop_validate_config(&cfg));
}

static void test_cascade_pi_moves_toward_setpoint(void) {
    bm_control_loop_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.dt_s = 0.01f;
    axis.config.outer_pi.kp = 2.0f;
    axis.config.outer_pi.ki = 5.0f;
    axis.config.outer_pi.out_min = -10.0f;
    axis.config.outer_pi.out_max = 10.0f;
    axis.config.outer_pi.integrator_min = -20.0f;
    axis.config.outer_pi.integrator_max = 20.0f;
    axis.config.inner_pi.kp = 3.0f;
    axis.config.inner_pi.ki = 8.0f;
    axis.config.inner_pi.out_min = -5.0f;
    axis.config.inner_pi.out_max = 5.0f;
    axis.config.inner_pi.integrator_min = -10.0f;
    axis.config.inner_pi.integrator_max = 10.0f;
    axis.resources.read_plant = read_plant;
    axis.resources.write_output = write_output;

    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_validate_config(&axis.config));
    bm_control_loop_reset(&axis);

    for (i = 0u; i < 500u; ++i) {
        bm_control_loop_step(&axis);
    }
    TEST_ASSERT_TRUE(g_outer_meas > 0.3f);
    TEST_ASSERT_TRUE(fabsf(g_last_output) <= 5.0f + 0.001f);
    TEST_ASSERT_TRUE(axis.state.step_count == 500u);
}

/* ---------------------------------------------------------------------------
 * 辅助：构造带有效配置的 axis，并填入资源回调
 * ---------------------------------------------------------------------------
 */
static void make_valid_axis(bm_control_loop_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.dt_s = 0.01f;
    axis->config.outer_pi.kp = 2.0f;
    axis->config.outer_pi.ki = 5.0f;
    axis->config.outer_pi.out_min = -10.0f;
    axis->config.outer_pi.out_max = 10.0f;
    axis->config.outer_pi.integrator_min = -20.0f;
    axis->config.outer_pi.integrator_max = 20.0f;
    axis->config.inner_pi.kp = 3.0f;
    axis->config.inner_pi.ki = 8.0f;
    axis->config.inner_pi.out_min = -5.0f;
    axis->config.inner_pi.out_max = 5.0f;
    axis->config.inner_pi.integrator_min = -10.0f;
    axis->config.inner_pi.integrator_max = 10.0f;
    axis->resources.read_plant = read_plant;
    axis->resources.write_output = write_output;
}

/* ---------------------------------------------------------------------------
 * exec 生命周期测试
 * ---------------------------------------------------------------------------
 */

/**
 * @brief exec_init 对 NULL instance 应返回 BM_ERR_INVALID
 */
static void test_exec_init_null_returns_invalid(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_control_loop_exec_init(NULL));
}

/**
 * @brief exec_init 对配置非法的 axis 应返回 BM_ERR_INVALID
 */
static void test_exec_init_bad_config_returns_invalid(void) {
    bm_control_loop_axis_t axis;
    bm_exec_t inst;

    memset(&axis, 0, sizeof(axis));
    /* dt_s = 0 → 配置非法 */
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_control_loop_exec_init(&inst));
}

/**
 * @brief exec_init 对有效配置应返回 BM_OK 并复位状态
 */
static void test_exec_init_valid_resets_state(void) {
    bm_control_loop_axis_t axis;
    bm_exec_t inst;

    make_valid_axis(&axis);
    /* 故意置入非零脏值 */
    axis.state.step_count = 99u;
    axis.state.outer_out = 3.14f;

    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_exec_init(&inst));
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.step_count);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.outer_out);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
}

/**
 * @brief exec_start 无论参数如何均返回 BM_OK
 */
static void test_exec_start_returns_ok(void) {
    bm_control_loop_axis_t axis;
    bm_exec_t inst;

    make_valid_axis(&axis);
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_exec_start(&inst));
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_exec_start(NULL));
}

/**
 * @brief exec_step 通过 instance->state 正确调用 step，step_count 递增
 */
static void test_exec_step_increments_step_count(void) {
    bm_control_loop_axis_t axis;
    bm_exec_t inst;

    make_valid_axis(&axis);
    bm_control_loop_reset(&axis);
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    g_setpoint = 1.0f;
    bm_control_loop_exec_step(&inst);
    bm_control_loop_exec_step(&inst);

    TEST_ASSERT_EQUAL_UINT32(2u, axis.state.step_count);
}

/**
 * @brief exec_safe_stop：输出归零、积分器复位、write_output 被调用写零
 */
static void test_exec_safe_stop_zeros_output(void) {
    bm_control_loop_axis_t axis;
    bm_exec_t inst;
    uint32_t i;

    make_valid_axis(&axis);
    bm_control_loop_reset(&axis);
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    g_setpoint = 1.0f;
    for (i = 0u; i < 100u; ++i) {
        bm_control_loop_exec_step(&inst);
    }

    /* 停机前应有非零输出 */
    TEST_ASSERT_TRUE(fabsf(g_last_output) > 0.0f);

    bm_control_loop_exec_safe_stop(&inst);

    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.outer_out);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
    /* write_output 回调已被调用并传入 0.0，g_last_output 应归零 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, g_last_output);
}

/**
 * @brief exec_safe_stop 对 NULL 不崩溃
 */
static void test_exec_safe_stop_null_no_crash(void) {
    /* 只要不崩溃即可 */
    bm_control_loop_exec_safe_stop(NULL);
}

/**
 * @brief ops 表指针与各函数符号一致
 */
static void test_exec_ops_table_pointers(void) {
    TEST_ASSERT_EQUAL_PTR(bm_control_loop_exec_init,
                          bm_control_loop_exec_ops.init);
    TEST_ASSERT_EQUAL_PTR(bm_control_loop_exec_start,
                          bm_control_loop_exec_ops.start);
    TEST_ASSERT_EQUAL_PTR(bm_control_loop_exec_safe_stop,
                          bm_control_loop_exec_ops.safe_stop);
}

/**
 * @brief 完整生命周期：init → start → step×N → safe_stop，step_count 正确
 */
static void test_exec_full_lifecycle(void) {
    bm_control_loop_axis_t axis;
    bm_exec_t inst;
    uint32_t i;

    make_valid_axis(&axis);
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    g_setpoint = 1.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_exec_ops.init(&inst));
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_exec_ops.start(&inst));

    for (i = 0u; i < 200u; ++i) {
        bm_control_loop_exec_step(&inst);
    }
    TEST_ASSERT_EQUAL_UINT32(200u, axis.state.step_count);

    bm_control_loop_exec_ops.safe_stop(&inst);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.outer_out);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
}

void test_control_loop(void) {
    RUN_TEST(test_validate_config_rejects_bad_dt);
    RUN_TEST(test_cascade_pi_moves_toward_setpoint);
    /* exec 生命周期 */
    RUN_TEST(test_exec_init_null_returns_invalid);
    RUN_TEST(test_exec_init_bad_config_returns_invalid);
    RUN_TEST(test_exec_init_valid_resets_state);
    RUN_TEST(test_exec_start_returns_ok);
    RUN_TEST(test_exec_step_increments_step_count);
    RUN_TEST(test_exec_safe_stop_zeros_output);
    RUN_TEST(test_exec_safe_stop_null_no_crash);
    RUN_TEST(test_exec_ops_table_pointers);
    RUN_TEST(test_exec_full_lifecycle);
}

int main(void) {
    UNITY_BEGIN();
    test_control_loop();
    return UNITY_END();
}

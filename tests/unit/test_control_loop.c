/**
 * @file test_control_loop.c
 * @brief control_loop 串级 PI 组件单元测试
 *
 * 覆盖外环设定、内环跟踪与输出饱和基本行为；
 * 并覆盖 exec 生命周期（init → start → step → safe_stop）路径；
 * 以及 CMD_ENABLED/FAULT 状态机。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.4
 * @date 2026-08-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补 exec 生命周期测试
 * 2026-07-27       1.2            zeh            新增 bm_control_loop_init 直接入口测试
 * 2026-08-01       1.3            zeh            补 ENABLED/FAULT 状态机用例；步进前须使能
 * 2026-08-02       1.4            zeh            对齐门控绑定 read_command 语义：default_disabled
 *                                                /enabled 用例改绑命令通道；新增未接通道恒使能
 *                                                legacy 兼容用例
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
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

static int read_plant_fail(void *user, float *outer_meas, float *inner_meas,
                           float *setpoint) {
    (void)user;
    (void)outer_meas;
    (void)inner_meas;
    (void)setpoint;
    return -1;
}

static int write_output(void *user, float output) {
    (void)user;
    g_last_output = output;
    g_inner_meas += (output - g_inner_meas) * 0.2f;
    g_outer_meas += (g_inner_meas - g_outer_meas) * 0.1f;
    return 0;
}

/** @brief 命令通道回调：永远"无新命令"（返回非零），用于绑定通道后
 *  验证使能门控（配合 apply_command 直发命令的场景）。 */
static int read_command_none(void *user, bm_control_loop_cmd_t *command) {
    (void)user;
    (void)command;
    return -1;
}

/** @brief 使能控制环（step 前须调用） */
static void enable_axis(bm_control_loop_axis_t *axis) {
    bm_control_loop_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = 1u;
    cmd.status = BM_CONTROL_LOOP_CMD_ENABLED;
    bm_control_loop_apply_command(axis, &cmd);
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
    enable_axis(&axis);

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

/**
 * @brief bm_control_loop_init 对 NULL 应返回 BM_ERR_INVALID
 */
static void test_init_null_returns_invalid(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_control_loop_init(NULL));
}

/**
 * @brief bm_control_loop_init 对有效配置应返回 BM_OK 并复位状态
 */
static void test_init_valid_resets_state(void) {
    bm_control_loop_axis_t axis;

    make_valid_axis(&axis);
    /* 故意置入非零脏值 */
    axis.state.step_count = 99u;
    axis.state.outer_out = 3.14f;

    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_init(&axis));
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.step_count);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.outer_out);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.cmd.status & BM_CONTROL_LOOP_CMD_ENABLED);
}

/**
 * @brief 绑定命令通道但默认未使能：step 不跑环、输出保持零
 */
static void test_default_disabled_no_output(void) {
    bm_control_loop_axis_t axis;

    make_valid_axis(&axis);
    axis.resources.read_command = read_command_none;
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_init(&axis));
    g_last_output = 99.0f;
    bm_control_loop_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, g_last_output);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.step_count);
}

/**
 * @brief 未接命令通道（read_command==NULL）保持恒使能 legacy 语义：
 *        不发任何命令 step 也跑环（2026-08-02 兼容性修正）
 */
static void test_no_command_channel_runs_legacy(void) {
    bm_control_loop_axis_t axis;

    make_valid_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_init(&axis));
    bm_control_loop_step(&axis);
    TEST_ASSERT_TRUE(fabsf(g_last_output) > 0.0f);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.step_count);
}

/**
 * @brief 绑定命令通道 + ENABLED 后正常步进
 */
static void test_enabled_produces_output(void) {
    bm_control_loop_axis_t axis;

    make_valid_axis(&axis);
    axis.resources.read_command = read_command_none;
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_init(&axis));
    enable_axis(&axis);
    bm_control_loop_step(&axis);
    TEST_ASSERT_TRUE(fabsf(g_last_output) > 0.0f);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.step_count);
}

/**
 * @brief FAULT 锁存后清输出；reset 清故障
 */
static void test_fault_latches_and_reset_clears(void) {
    bm_control_loop_axis_t axis;
    bm_control_loop_cmd_t cmd;

    make_valid_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_init(&axis));
    enable_axis(&axis);
    bm_control_loop_step(&axis);
    TEST_ASSERT_TRUE(fabsf(axis.state.inner_out) > 0.0f);

    memset(&cmd, 0, sizeof(cmd));
    cmd.status = BM_CONTROL_LOOP_CMD_ENABLED | BM_CONTROL_LOOP_CMD_FAULT;
    bm_control_loop_apply_command(&axis, &cmd);
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, g_last_output);

    bm_control_loop_step(&axis);
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);

    bm_control_loop_reset(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.cmd.status);
}

/**
 * @brief read_plant 失败锁存故障
 */
static void test_read_plant_fail_latches_fault(void) {
    bm_control_loop_axis_t axis;

    make_valid_axis(&axis);
    axis.resources.read_plant = read_plant_fail;
    TEST_ASSERT_EQUAL(BM_OK, bm_control_loop_init(&axis));
    enable_axis(&axis);
    bm_control_loop_step(&axis);
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.inner_out);
}

/**
 * @brief apply_command / step NULL 安全
 */
static void test_null_safety(void) {
    bm_control_loop_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    bm_control_loop_apply_command(NULL, &cmd);
    bm_control_loop_apply_command(NULL, NULL);
    bm_control_loop_step(NULL);
    bm_control_loop_reset(NULL);
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
    enable_axis(&axis);
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
    enable_axis(&axis);
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
    enable_axis(&axis);

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
    RUN_TEST(test_init_null_returns_invalid);
    RUN_TEST(test_init_valid_resets_state);
    RUN_TEST(test_default_disabled_no_output);
    RUN_TEST(test_no_command_channel_runs_legacy);
    RUN_TEST(test_enabled_produces_output);
    RUN_TEST(test_fault_latches_and_reset_clears);
    RUN_TEST(test_read_plant_fail_latches_fault);
    RUN_TEST(test_null_safety);
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

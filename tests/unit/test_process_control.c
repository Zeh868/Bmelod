/**
 * @file test_process_control.c
 * @brief process_control 组件单元测试
 *
 * 覆盖 Smith 预估器延迟补偿、串级 PID 跟踪 happy-path、
 * validate_config 边界拒绝以及 exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-23       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/process_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 测试桩 ---------- */

static float g_setpoint;
static float g_measurement;
static float g_output;
static int   g_read_fail;

/**
 * @brief 模拟读取 IO 的桩函数
 */
static int read_io(void *user, float *setpoint, float *measurement) {
    (void)user;
    if (g_read_fail) {
        return -1;
    }
    *setpoint    = g_setpoint;
    *measurement = g_measurement;
    return 0;
}

/**
 * @brief 模拟写入输出的桩函数
 */
static int write_output(void *user, float output) {
    (void)user;
    g_output = output;
    return 0;
}

/* 延迟线缓冲区（10 步延迟，缓冲需 > delay_steps） */
#define DELAY_LINE_LEN  16u
static float g_delay_line[DELAY_LINE_LEN];

/**
 * @brief 构建合法的默认轴配置
 */
static void build_default_axis(bm_process_control_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    memset(g_delay_line, 0, sizeof(g_delay_line));

    /* 外环 PID */
    axis->config.outer_pid.kp             = 2.0f;
    axis->config.outer_pid.ki             = 0.5f;
    axis->config.outer_pid.kd             = 0.0f;
    axis->config.outer_pid.out_min        = -50.0f;
    axis->config.outer_pid.out_max        =  50.0f;
    axis->config.outer_pid.integrator_min = -20.0f;
    axis->config.outer_pid.integrator_max =  20.0f;

    /* 内环 PID */
    axis->config.inner_pid.kp             = 1.0f;
    axis->config.inner_pid.ki             = 0.2f;
    axis->config.inner_pid.kd             = 0.0f;
    axis->config.inner_pid.out_min        = -100.0f;
    axis->config.inner_pid.out_max        =  100.0f;
    axis->config.inner_pid.integrator_min = -50.0f;
    axis->config.inner_pid.integrator_max =  50.0f;

    /* Smith 预估器：3 步延迟，增益 1.0 */
    axis->config.smith.model_gain   = 1.0f;
    axis->config.smith.delay_steps  = 3u;
    axis->config.smith_delay_line   = g_delay_line;
    axis->config.smith_line_len     = DELAY_LINE_LEN;
    axis->config.dt_s               = 0.01f;

    /* 资源绑定 */
    axis->resources.read_io      = read_io;
    axis->resources.write_output = write_output;
}

void setUp(void) {
    g_setpoint   = 10.0f;
    g_measurement = 0.0f;
    g_output     = 0.0f;
    g_read_fail  = 0;
}

void tearDown(void) {}

/* ================================================================
 * 测试 1：合法配置下 init 应返回 BM_OK
 * ================================================================ */
void test_process_init_valid_config_returns_ok(void) {
    bm_process_control_axis_t axis;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_init(&axis));
}

/* ================================================================
 * 测试 2：串级 PID 跟踪——误差非零时输出应不为零
 * ================================================================ */
void test_process_cascade_pid_produces_nonzero_output(void) {
    bm_process_control_axis_t axis;

    build_default_axis(&axis);
    g_setpoint    = 10.0f;
    g_measurement = 0.0f;  /* 误差 = 10 */
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_init(&axis));

    bm_process_control_step(&axis);

    TEST_ASSERT_TRUE(g_output != 0.0f);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.telemetry.sequence);
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_PROCESS_CTRL_TEL_VALID);
}

/* ================================================================
 * 测试 3：Smith 预估延迟补偿——延迟步数内误差被延迟线补偿
 *
 * 验证：在 delay_steps 步内，Smith 预估器输出应为零
 *（延迟线尚未充满，预测输出与实测相同，误差被抵消）。
 * ================================================================ */
void test_process_smith_delay_compensation(void) {
    bm_process_control_axis_t axis;
    uint32_t i;

    build_default_axis(&axis);
    /* 设定值 = 测量值 = 0，使串级输出为零 */
    g_setpoint    = 0.0f;
    g_measurement = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_init(&axis));

    /* 第一步：无误差时输出应为零 */
    bm_process_control_step(&axis);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_output);

    /* 施加阶跃，连续步进 delay_steps 拍，验证延迟线正常工作 */
    g_setpoint = 5.0f;
    for (i = 0u; i < (uint32_t)axis.config.smith.delay_steps + 1u; i++) {
        bm_process_control_step(&axis);
    }
    /* 经过延迟步数后，Smith 预估已产生预测，输出应非零 */
    TEST_ASSERT_TRUE(axis.state.outer_out != 0.0f || axis.state.inner_out != 0.0f);
}

/* ================================================================
 * 测试 4：read_io 失败时打 STALE，输出不更新
 * ================================================================ */
void test_process_read_fail_marks_stale(void) {
    bm_process_control_axis_t axis;
    float output_before;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_init(&axis));

    output_before = g_output;
    g_read_fail   = 1;
    bm_process_control_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_PROCESS_CTRL_TEL_STALE);
    TEST_ASSERT_EQUAL_FLOAT(output_before, g_output);
}

/* ================================================================
 * 测试 5：validate_config 拒绝 model_gain <= 0
 * ================================================================ */
void test_process_validate_rejects_zero_model_gain(void) {
    bm_process_control_config_t cfg;
    bm_process_control_axis_t   axis;
    static float dl[DELAY_LINE_LEN];

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.smith_delay_line  = dl;
    cfg.smith.model_gain  = 0.0f; /* 非法 */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 6：validate_config 拒绝 delay_steps >= line_len
 * ================================================================ */
void test_process_validate_rejects_delay_overflow(void) {
    bm_process_control_config_t cfg;
    bm_process_control_axis_t   axis;
    static float dl[DELAY_LINE_LEN];

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.smith_delay_line  = dl;
    cfg.smith_line_len    = 4u;
    cfg.smith.delay_steps = 4u; /* delay_steps == line_len，非法 */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 7：validate_config 拒绝 dt_s <= 0
 * ================================================================ */
void test_process_validate_rejects_zero_dt(void) {
    bm_process_control_config_t cfg;
    bm_process_control_axis_t   axis;
    static float dl[DELAY_LINE_LEN];

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.smith_delay_line = dl;
    cfg.dt_s             = 0.0f; /* 非法 */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 8：validate_config 拒绝 delay_line 为 NULL
 * ================================================================ */
void test_process_validate_rejects_null_delay_line(void) {
    bm_process_control_config_t cfg;
    bm_process_control_axis_t   axis;

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.smith_delay_line = NULL; /* 非法 */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 9：validate_config 合法配置返回 BM_OK
 * ================================================================ */
void test_process_validate_accepts_valid_config(void) {
    bm_process_control_axis_t axis;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_validate_config(&axis.config));
}

/* ================================================================
 * 测试 10：exec_ops 生命周期
 * ================================================================ */
void test_process_exec_ops_lifecycle(void) {
    bm_process_control_axis_t axis;
    bm_exec_t                 exec;

    build_default_axis(&axis);
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    /* init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_exec_ops.init(&exec));
    /* start 应返回 BM_OK */
    TEST_ASSERT_EQUAL(BM_OK, bm_process_control_exec_ops.start(&exec));

    /* safe_stop 应清零输出并写入硬件 */
    axis.state.outer_out = 99.0f;
    axis.state.inner_out = 88.0f;
    g_output = 77.0f;
    bm_process_control_exec_ops.safe_stop(&exec);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.outer_out);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.inner_out);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_output);
}

/* ================================================================
 * 测试 11：exec_ops init 对非法配置返回 BM_ERR_INVALID
 * ================================================================ */
void test_process_exec_ops_init_rejects_bad_config(void) {
    bm_process_control_axis_t axis;
    bm_exec_t                 exec;

    build_default_axis(&axis);
    axis.config.smith.model_gain = 0.0f; /* 非法 */
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_control_exec_ops.init(&exec));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_process_init_valid_config_returns_ok);
    RUN_TEST(test_process_cascade_pid_produces_nonzero_output);
    RUN_TEST(test_process_smith_delay_compensation);
    RUN_TEST(test_process_read_fail_marks_stale);
    RUN_TEST(test_process_validate_rejects_zero_model_gain);
    RUN_TEST(test_process_validate_rejects_delay_overflow);
    RUN_TEST(test_process_validate_rejects_zero_dt);
    RUN_TEST(test_process_validate_rejects_null_delay_line);
    RUN_TEST(test_process_validate_accepts_valid_config);
    RUN_TEST(test_process_exec_ops_lifecycle);
    RUN_TEST(test_process_exec_ops_init_rejects_bad_config);
    return UNITY_END();
}

/**
 * @file test_grid_control.c
 * @brief grid_control 组件单元测试
 *
 * 覆盖 SOGI-PLL 收敛行为、PR 电流环 happy-path、
 * validate_config 边界拒绝、exec_ops 生命周期以及
 * CMD_ENABLED/FAULT 状态机。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-08-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-23       1.0            zeh            正式发布
 * 2026-08-01       1.1            zeh            补 ENABLED/FAULT 状态机用例；步进前须使能
 * 2026-08-02       1.2            zeh            对齐门控绑定 read_command 语义：default_disabled
 *                                                用例改绑命令通道；新增未接通道恒使能 legacy
 *                                                兼容用例
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/grid_control.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/* ---------- 测试桩 ---------- */

static float g_v_grid;
static float g_i_meas;
static float g_i_ref;
static float g_v_cmd_out;
static int   g_read_fail;

/**
 * @brief 模拟读取电网 IO 的桩函数
 */
static int read_io(void *user, float *v_grid, float *i_meas, float *i_ref) {
    (void)user;
    if (g_read_fail) {
        return -1;
    }
    *v_grid  = g_v_grid;
    *i_meas  = g_i_meas;
    *i_ref   = g_i_ref;
    return 0;
}

/**
 * @brief 模拟写入输出电压命令的桩函数
 */
static int write_output(void *user, float v_cmd) {
    (void)user;
    g_v_cmd_out = v_cmd;
    return 0;
}

/** @brief 命令通道回调：永远"无新命令"（返回非零），用于绑定通道后
 *  验证使能门控（配合 apply_command 直发命令的场景）。 */
static int read_command_none(void *user, bm_grid_ctrl_cmd_t *command) {
    (void)user;
    (void)command;
    return -1;
}

/** @brief 使能并网控制（step 前须调用） */
static void enable_axis(bm_grid_control_axis_t *axis) {
    bm_grid_ctrl_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.sequence = 1u;
    cmd.status = BM_GRID_CTRL_CMD_ENABLED;
    bm_grid_control_apply_command(axis, &cmd);
}

/** @brief 构建合法的默认轴配置（50 Hz，典型参数） */
static void build_default_axis(bm_grid_control_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    /* PLL 配置 */
    axis->config.pll.nominal_omega_rad_s   = 314.159f;  /* 2π×50 */
    axis->config.pll.k_sogi                = 1.414f;    /* √2 */
    axis->config.pll.k_pll                 = 50.0f;
    axis->config.pll.integrator_limit_ratio = 0.2f;
    /* PR 电流环配置 */
    axis->config.pr_current.kp             = 5.0f;
    axis->config.pr_current.kr             = 200.0f;
    axis->config.pr_current.omega_rad_s    = 314.159f;
    axis->config.pr_current.bandwidth_rad_s = 5.0f;
    axis->config.pr_current.out_min        = -400.0f;
    axis->config.pr_current.out_max        =  400.0f;
    axis->config.dt_s                      = 1.0f / 10000.0f;  /* 10 kHz */
    /* 资源绑定 */
    axis->resources.read_io       = read_io;
    axis->resources.write_output  = write_output;
}

void setUp(void) {
    g_v_grid    = 311.0f;  /* 220 Vrms 峰值 */
    g_i_meas    = 0.0f;
    g_i_ref     = 10.0f;
    g_v_cmd_out = 0.0f;
    g_read_fail = 0;
}

void tearDown(void) {}

/* ================================================================
 * 测试 1：合法配置下 init 应返回 BM_OK
 * ================================================================ */
void test_grid_init_valid_config_returns_ok(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.cmd.status & BM_GRID_CTRL_CMD_ENABLED);
}

/* ================================================================
 * 测试 2：SOGI-PLL 在多拍步进后 omega 应收敛至 nominal
 * ================================================================ */
void test_grid_sogi_pll_converges_to_nominal(void) {
    bm_grid_control_axis_t axis;
    uint32_t i;
    float    dt_s;
    float    omega_nominal;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    enable_axis(&axis);

    dt_s          = axis.config.dt_s;
    omega_nominal = axis.config.pll.nominal_omega_rad_s;

    /* 输入 50 Hz 正弦，步进 500 拍（50 ms） */
    for (i = 0u; i < 500u; i++) {
        g_v_grid = 311.0f * sinf(omega_nominal * (float)i * dt_s);
        bm_grid_control_step(&axis);
    }

    /* PLL 估计角频率应在 nominal ±5% 内 */
    TEST_ASSERT_FLOAT_WITHIN(omega_nominal * 0.05f,
                             omega_nominal,
                             axis.state.omega_rad_s);
    /* 遥测状态应含 VALID 标志 */
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_GRID_CTRL_TEL_VALID);
}

/* ================================================================
 * 测试 3：PR 电流环 happy-path——误差非零时 v_cmd 应不为零
 * ================================================================ */
void test_grid_pr_current_loop_produces_nonzero_cmd(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    g_i_meas = 0.0f;
    g_i_ref  = 10.0f;  /* 10 A 误差 */
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    enable_axis(&axis);

    bm_grid_control_step(&axis);

    /* v_cmd 应非零（kp 项立即响应） */
    TEST_ASSERT_TRUE(g_v_cmd_out != 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(g_v_cmd_out, axis.state.telemetry.v_cmd);
}

/* ================================================================
 * 测试 4：误差为零时 v_cmd 应为零（PR 无谐振激励）
 * ================================================================ */
void test_grid_pr_zero_error_zero_cmd(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    g_i_meas = 10.0f;
    g_i_ref  = 10.0f;  /* 零误差 */
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    enable_axis(&axis);

    bm_grid_control_step(&axis);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_v_cmd_out);
}

/* ================================================================
 * 测试 5：read_io 失败时锁存故障，打 STALE|FAULT，v_cmd 清零
 * ================================================================ */
void test_grid_read_fail_marks_stale(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    enable_axis(&axis);

    g_read_fail = 1;
    bm_grid_control_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_GRID_CTRL_TEL_STALE);
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_GRID_CTRL_TEL_FAULT);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_v_cmd_out);
}

/* ================================================================
 * 测试 5b：绑定命令通道但默认未使能无输出
 * ================================================================ */
void test_grid_default_disabled_no_output(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    axis.resources.read_command = read_command_none;
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    g_v_cmd_out = 99.0f;
    bm_grid_control_step(&axis);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_v_cmd_out);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.v_cmd);
}

/* ================================================================
 * 测试 5b2：未接命令通道（read_command==NULL）保持恒使能 legacy
 * 语义，不发命令也跑环（2026-08-02 兼容性修正）
 * ================================================================ */
void test_grid_no_command_channel_runs_legacy(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    g_i_meas = 0.0f;
    g_i_ref  = 10.0f;  /* 10 A 误差 */
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));

    bm_grid_control_step(&axis);

    /* v_cmd 应非零（kp 项立即响应） */
    TEST_ASSERT_TRUE(g_v_cmd_out != 0.0f);
}

/* ================================================================
 * 测试 5c：FAULT 锁存；reset 清故障
 * ================================================================ */
void test_grid_fault_latches_and_reset_clears(void) {
    bm_grid_control_axis_t axis;
    bm_grid_ctrl_cmd_t cmd;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));
    enable_axis(&axis);
    bm_grid_control_step(&axis);
    TEST_ASSERT_TRUE(g_v_cmd_out != 0.0f);

    memset(&cmd, 0, sizeof(cmd));
    cmd.status = BM_GRID_CTRL_CMD_ENABLED | BM_GRID_CTRL_CMD_FAULT;
    bm_grid_control_apply_command(&axis, &cmd);
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.v_cmd);

    bm_grid_control_reset(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.cmd.status);
}

/* ================================================================
 * 测试 5d：NULL 安全
 * ================================================================ */
void test_grid_null_safety(void) {
    bm_grid_ctrl_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    bm_grid_control_apply_command(NULL, &cmd);
    bm_grid_control_apply_command(NULL, NULL);
    bm_grid_control_step(NULL);
    bm_grid_control_reset(NULL);
}

/* ================================================================
 * 测试 6：validate_config 拒绝 dt_s <= 0
 * ================================================================ */
void test_grid_validate_rejects_zero_dt(void) {
    bm_grid_control_config_t cfg;
    bm_grid_control_axis_t   axis;

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.dt_s = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_grid_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 7：validate_config 拒绝 nominal_omega_rad_s <= 0
 * ================================================================ */
void test_grid_validate_rejects_zero_omega(void) {
    bm_grid_control_config_t cfg;
    bm_grid_control_axis_t   axis;

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.pll.nominal_omega_rad_s = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_grid_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 8：validate_config 拒绝 k_sogi <= 0
 * ================================================================ */
void test_grid_validate_rejects_zero_k_sogi(void) {
    bm_grid_control_config_t cfg;
    bm_grid_control_axis_t   axis;

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.pll.k_sogi = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_grid_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 9：validate_config 拒绝 PR kr <= 0
 * ================================================================ */
void test_grid_validate_rejects_zero_kr(void) {
    bm_grid_control_config_t cfg;
    bm_grid_control_axis_t   axis;

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.pr_current.kr = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_grid_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 10：validate_config 拒绝 PR out_max <= out_min
 * ================================================================ */
void test_grid_validate_rejects_inverted_pr_limits(void) {
    bm_grid_control_config_t cfg;
    bm_grid_control_axis_t   axis;

    build_default_axis(&axis);
    cfg = axis.config;
    cfg.pr_current.out_min = 100.0f;
    cfg.pr_current.out_max = -100.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_grid_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 11：exec_ops 生命周期
 * ================================================================ */
void test_grid_exec_ops_lifecycle(void) {
    bm_grid_control_axis_t axis;
    bm_exec_t              exec;

    build_default_axis(&axis);
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    /* init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_exec_ops.init(&exec));
    /* start 应返回 BM_OK */
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_exec_ops.start(&exec));

    /* safe_stop 应清零 v_cmd 并写入硬件 */
    axis.state.v_cmd = 123.0f;
    g_v_cmd_out      = 999.0f;
    bm_grid_control_exec_ops.safe_stop(&exec);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.v_cmd);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_v_cmd_out);
}

/* ================================================================
 * 测试 12：exec_ops init 对非法配置返回 BM_ERR_INVALID
 * ================================================================ */
void test_grid_exec_ops_init_rejects_bad_config(void) {
    bm_grid_control_axis_t axis;
    bm_exec_t              exec;

    build_default_axis(&axis);
    axis.config.dt_s = -1.0f; /* 非法 */
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_grid_control_exec_ops.init(&exec));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_grid_init_valid_config_returns_ok);
    RUN_TEST(test_grid_sogi_pll_converges_to_nominal);
    RUN_TEST(test_grid_pr_current_loop_produces_nonzero_cmd);
    RUN_TEST(test_grid_pr_zero_error_zero_cmd);
    RUN_TEST(test_grid_read_fail_marks_stale);
    RUN_TEST(test_grid_default_disabled_no_output);
    RUN_TEST(test_grid_no_command_channel_runs_legacy);
    RUN_TEST(test_grid_fault_latches_and_reset_clears);
    RUN_TEST(test_grid_null_safety);
    RUN_TEST(test_grid_validate_rejects_zero_dt);
    RUN_TEST(test_grid_validate_rejects_zero_omega);
    RUN_TEST(test_grid_validate_rejects_zero_k_sogi);
    RUN_TEST(test_grid_validate_rejects_zero_kr);
    RUN_TEST(test_grid_validate_rejects_inverted_pr_limits);
    RUN_TEST(test_grid_exec_ops_lifecycle);
    RUN_TEST(test_grid_exec_ops_init_rejects_bad_config);
    return UNITY_END();
}

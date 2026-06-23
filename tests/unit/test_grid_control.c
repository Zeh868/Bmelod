/**
 * @file test_grid_control.c
 * @brief grid_control 组件单元测试
 *
 * 覆盖 SOGI-PLL 收敛行为、PR 电流环 happy-path、
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
}

/* ================================================================
 * 测试 2：SOGI-PLL 在多拍步进后 omega 应收敛至 nominal
 *
 * 输入为固定频率正弦（50 Hz），经过足够拍数 PLL 估计角频率
 * 应向 nominal_omega_rad_s 收敛（允许 5% 误差范围）。
 * ================================================================ */
void test_grid_sogi_pll_converges_to_nominal(void) {
    bm_grid_control_axis_t axis;
    uint32_t i;
    float    dt_s;
    float    omega_nominal;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));

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

    bm_grid_control_step(&axis);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_v_cmd_out);
}

/* ================================================================
 * 测试 5：read_io 失败时打 STALE，v_cmd 清零
 * ================================================================ */
void test_grid_read_fail_marks_stale(void) {
    bm_grid_control_axis_t axis;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_grid_control_init(&axis));

    g_read_fail = 1;
    bm_grid_control_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_GRID_CTRL_TEL_STALE);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_v_cmd_out);
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
    RUN_TEST(test_grid_validate_rejects_zero_dt);
    RUN_TEST(test_grid_validate_rejects_zero_omega);
    RUN_TEST(test_grid_validate_rejects_zero_k_sogi);
    RUN_TEST(test_grid_validate_rejects_zero_kr);
    RUN_TEST(test_grid_validate_rejects_inverted_pr_limits);
    RUN_TEST(test_grid_exec_ops_lifecycle);
    RUN_TEST(test_grid_exec_ops_init_rejects_bad_config);
    return UNITY_END();
}

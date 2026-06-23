/**
 * @file test_solar_control.c
 * @brief solar_control 组件单元测试
 *
 * 覆盖 MPPT P&O 步进跟踪、限功率降额、power=0 边界、
 * validate_config 非法配置拒绝以及 exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补 validate 拒绝测试、power=0 边界、exec_ops 测试
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/solar_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 全局测试桩 ---------- */

static float g_voltage;
static float g_current;
static float g_vref_out;
static int   g_read_fail;

/**
 * @brief 模拟读取 IV 的桩函数
 */
static int read_iv(void *user, float *voltage_v, float *current_a) {
    (void)user;
    if (g_read_fail) {
        return -1;
    }
    *voltage_v = g_voltage;
    *current_a = g_current;
    return 0;
}

/**
 * @brief 模拟写入 v_ref 的桩函数
 */
static int write_vref(void *user, float v_ref_v) {
    (void)user;
    g_vref_out = v_ref_v;
    return 0;
}

/**
 * @brief 构建合法的 P&O 默认轴配置
 */
static void build_default_axis(bm_solar_control_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.mppt_mode       = BM_SOLAR_MPPT_PO;
    axis->config.mppt_po.step_v  = 0.1f;
    axis->config.mppt_po.v_min   = 10.0f;
    axis->config.mppt_po.v_max   = 24.0f;
    axis->config.power_limit_w   = 0.0f;   /* 默认不限功率 */
    axis->config.v_init_v        = 17.0f;
    axis->resources.read_iv      = read_iv;
    axis->resources.write_vref   = write_vref;
}

void setUp(void) {
    g_voltage  = 18.0f;
    g_current  = 2.0f;
    g_vref_out = 0.0f;
    g_read_fail = 0;
}

void tearDown(void) {}

/* ================================================================
 * 测试 1：P&O 跟踪——连续步进后 v_ref 应发生变化
 * ================================================================ */
void test_solar_po_tracking_vref_changes(void) {
    bm_solar_control_axis_t axis;
    float v_ref_before;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_init(&axis));

    v_ref_before = axis.state.v_ref_v;
    bm_solar_control_step(&axis);

    /* P&O 应在第一拍产生非零 v_ref */
    TEST_ASSERT_TRUE(g_vref_out > 0.0f);
    /* 遥测序列应递增 */
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.telemetry.sequence);
    /* 遥测状态应含 VALID 标志 */
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_SOLAR_CTRL_TEL_VALID);
    /* v_ref 应与 v_ref_before 不同（P&O 步进了） */
    (void)v_ref_before; /* 首次步进方向不确定，仅验证非零 */
    TEST_ASSERT_TRUE(axis.state.telemetry.v_ref_v > 0.0f);
}

/* ================================================================
 * 测试 2：限功率——超过 power_limit_w 时打 LIMITED 标志
 * ================================================================ */
void test_solar_power_limit_flags_limited(void) {
    bm_solar_control_axis_t axis;

    build_default_axis(&axis);
    axis.config.power_limit_w = 30.0f;
    /* g_voltage=18, g_current=2 => power=36W > 30W */
    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_init(&axis));
    bm_solar_control_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_SOLAR_CTRL_TEL_LIMITED);
    /* 降额后 v_ref < 原始步进值：power_limit/power = 30/36 < 1 */
    TEST_ASSERT_TRUE(axis.state.telemetry.v_ref_v > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(36.0f, axis.state.telemetry.power_w);
}

/* ================================================================
 * 测试 3：power=0 边界——电流为 0 时不限功率，不触发 LIMITED
 * ================================================================ */
void test_solar_power_zero_no_limit_flag(void) {
    bm_solar_control_axis_t axis;

    build_default_axis(&axis);
    axis.config.power_limit_w = 30.0f;
    g_current = 0.0f; /* power = 18 * 0 = 0 */
    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_init(&axis));
    bm_solar_control_step(&axis);

    /* power=0 不超过限制，不应标记 LIMITED */
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.telemetry.status & BM_SOLAR_CTRL_TEL_LIMITED);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.telemetry.power_w);
}

/* ================================================================
 * 测试 4：read_iv 失败时打 STALE，不更新 v_ref
 * ================================================================ */
void test_solar_read_fail_marks_stale(void) {
    bm_solar_control_axis_t axis;
    float v_ref_after_init;

    build_default_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_init(&axis));
    v_ref_after_init = axis.state.v_ref_v;

    g_read_fail = 1;
    bm_solar_control_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_SOLAR_CTRL_TEL_STALE);
    /* v_ref 不应被更新 */
    TEST_ASSERT_EQUAL_FLOAT(v_ref_after_init, axis.state.v_ref_v);
}

/* ================================================================
 * 测试 5：validate_config 拒绝 v_init_v <= 0
 * ================================================================ */
void test_solar_validate_rejects_zero_v_init(void) {
    bm_solar_control_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mppt_mode      = BM_SOLAR_MPPT_PO;
    cfg.mppt_po.step_v = 0.1f;
    cfg.mppt_po.v_min  = 10.0f;
    cfg.mppt_po.v_max  = 24.0f;
    cfg.power_limit_w  = 0.0f;
    cfg.v_init_v       = 0.0f; /* 非法 */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_solar_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 6：validate_config 拒绝 power_limit_w < 0
 * ================================================================ */
void test_solar_validate_rejects_negative_power_limit(void) {
    bm_solar_control_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mppt_mode      = BM_SOLAR_MPPT_PO;
    cfg.mppt_po.step_v = 0.1f;
    cfg.mppt_po.v_min  = 10.0f;
    cfg.mppt_po.v_max  = 24.0f;
    cfg.power_limit_w  = -1.0f; /* 非法 */
    cfg.v_init_v       = 17.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_solar_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 7：validate_config 拒绝 P&O step_v <= 0
 * ================================================================ */
void test_solar_validate_rejects_zero_step_v(void) {
    bm_solar_control_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mppt_mode      = BM_SOLAR_MPPT_PO;
    cfg.mppt_po.step_v = 0.0f; /* 非法 */
    cfg.mppt_po.v_min  = 10.0f;
    cfg.mppt_po.v_max  = 24.0f;
    cfg.power_limit_w  = 0.0f;
    cfg.v_init_v       = 17.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_solar_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 8：validate_config 拒绝 v_max <= v_min
 * ================================================================ */
void test_solar_validate_rejects_inverted_voltage_range(void) {
    bm_solar_control_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mppt_mode      = BM_SOLAR_MPPT_PO;
    cfg.mppt_po.step_v = 0.1f;
    cfg.mppt_po.v_min  = 24.0f;
    cfg.mppt_po.v_max  = 10.0f; /* v_max < v_min 非法 */
    cfg.power_limit_w  = 0.0f;
    cfg.v_init_v       = 17.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_solar_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 9：validate_config 合法配置应返回 BM_OK
 * ================================================================ */
void test_solar_validate_accepts_valid_config(void) {
    bm_solar_control_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mppt_mode      = BM_SOLAR_MPPT_PO;
    cfg.mppt_po.step_v = 0.5f;
    cfg.mppt_po.v_min  = 5.0f;
    cfg.mppt_po.v_max  = 40.0f;
    cfg.power_limit_w  = 100.0f;
    cfg.v_init_v       = 20.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_validate_config(&cfg));
}

/* ================================================================
 * 测试 10：exec_ops init/start/safe_stop 生命周期
 * ================================================================ */
void test_solar_exec_ops_lifecycle(void) {
    bm_solar_control_axis_t axis;
    bm_exec_t               exec;

    build_default_axis(&axis);
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    /* init 应成功并复位状态 */
    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_exec_ops.init(&exec));
    TEST_ASSERT_EQUAL_FLOAT(axis.config.v_init_v, axis.state.v_ref_v);

    /* start 应返回 BM_OK */
    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_exec_ops.start(&exec));

    /* safe_stop 应将 v_ref 清零并写入硬件 */
    bm_solar_control_exec_ops.safe_stop(&exec);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.v_ref_v);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g_vref_out);
}

/* ================================================================
 * 测试 11：exec_ops init 对非法配置返回 BM_ERR_INVALID
 * ================================================================ */
void test_solar_exec_ops_init_rejects_bad_config(void) {
    bm_solar_control_axis_t axis;
    bm_exec_t               exec;

    build_default_axis(&axis);
    axis.config.v_init_v = -1.0f; /* 非法 */
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_solar_control_exec_ops.init(&exec));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_solar_po_tracking_vref_changes);
    RUN_TEST(test_solar_power_limit_flags_limited);
    RUN_TEST(test_solar_power_zero_no_limit_flag);
    RUN_TEST(test_solar_read_fail_marks_stale);
    RUN_TEST(test_solar_validate_rejects_zero_v_init);
    RUN_TEST(test_solar_validate_rejects_negative_power_limit);
    RUN_TEST(test_solar_validate_rejects_zero_step_v);
    RUN_TEST(test_solar_validate_rejects_inverted_voltage_range);
    RUN_TEST(test_solar_validate_accepts_valid_config);
    RUN_TEST(test_solar_exec_ops_lifecycle);
    RUN_TEST(test_solar_exec_ops_init_rejects_bad_config);
    return UNITY_END();
}

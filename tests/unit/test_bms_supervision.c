/**
 * @file test_bms_supervision.c
 * @brief bms_supervision 组件单元测试
 *
 * 覆盖：正常恢复路径、read_sample 失败 → STALE、
 * 故障触发 → 降额 → 恢复全路径、exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始测试
 * 2026-06-23       1.1            zeh            补错误路径、降额全路径、exec_ops 测试
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/common/bm_types.h"
#include "bm/component/bms_supervision.h"

#include <string.h>

/* ---------- 桩数据 ---------- */

static float g_voltage_v;
static float g_current_a;
static float g_temp_c;
static int   g_read_fail;   /**< 非零时 read_sample 返回 -1 */
static uint32_t g_tel_count;

static int read_sample(void *user,
                       float *voltage_v,
                       float *current_a,
                       float *temp_c) {
    (void)user;
    if (g_read_fail) {
        return -1;
    }
    *voltage_v = g_voltage_v;
    *current_a = g_current_a;
    *temp_c    = g_temp_c;
    return 0;
}

static void publish_tel(void *user,
                        const bm_bms_supervision_telemetry_t *tel) {
    (void)user;
    (void)tel;
    g_tel_count++;
}

/* ---------- 辅助：构建合法轴配置 ---------- */

static void build_axis(bm_bms_supervision_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.v_max_v             = 4.2f;
    axis->config.v_min_v             = 2.5f;
    axis->config.i_max_a             = 100.0f;
    axis->config.temp_max_c          = 60.0f;
    axis->config.dt_s                = 0.01f;
    axis->config.derate_ramp.rate_per_s = 10.0f;
    axis->config.recovery_time_s     = 0.04f;
    axis->config.derate_target       = 0.5f;
    axis->resources.read_sample      = read_sample;
    axis->resources.publish_telemetry = publish_tel;
}

void setUp(void) {
    g_voltage_v = 3.7f;
    g_current_a = 0.0f;
    g_temp_c    = 25.0f;
    g_read_fail = 0;
    g_tel_count = 0u;
}

void tearDown(void) {}

/* ================================================================
 * 测试 1：正常情况：fault 触发 → 恢复后 derate_factor = 1.0
 * ================================================================ */
void test_bms_supervision_clears_fault_latch_when_normal(void) {
    bm_bms_supervision_axis_t axis;
    uint32_t i;

    build_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_init(&axis));

    /* 触发过压故障 */
    g_voltage_v = 4.5f;
    bm_bms_supervision_step(&axis);
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.limit_flags);
    TEST_ASSERT_EQUAL(1, axis.state.derating.state.fault_latched);

    /* 恢复正常电压，故障应解锁 */
    g_voltage_v = 3.7f;
    bm_bms_supervision_step(&axis);
    TEST_ASSERT_EQUAL(0u, axis.state.limit_flags);
    TEST_ASSERT_EQUAL(0, axis.state.derating.state.fault_latched);

    /* 多步后 derate_factor 应恢复到 1.0 */
    for (i = 0u; i < 50u; ++i) {
        bm_bms_supervision_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, axis.state.derating.state.derate_factor);
}

/* ================================================================
 * 测试 2：read_sample 失败 → 遥测打 STALE，step_count 仍递增
 * ================================================================ */
void test_bms_supervision_read_fail_marks_stale(void) {
    bm_bms_supervision_axis_t axis;

    build_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_init(&axis));

    g_read_fail = 1;
    bm_bms_supervision_step(&axis);

    /* status 应含 STALE 标志（bit2） */
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_BMS_SUP_TEL_STALE);
    /* sequence 应递增为 1 */
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.telemetry.sequence);
    /* 遥测回调应被调用一次 */
    TEST_ASSERT_EQUAL_UINT32(1u, g_tel_count);
}

/* ================================================================
 * 测试 3：read_sample 失败后不修改物理量（保持上次值）
 * ================================================================ */
void test_bms_supervision_read_fail_preserves_last_values(void) {
    bm_bms_supervision_axis_t axis;

    build_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_init(&axis));

    /* 先成功取一次采样，记录物理量 */
    g_voltage_v = 3.8f;
    g_current_a = 5.0f;
    g_temp_c    = 30.0f;
    g_read_fail = 0;
    bm_bms_supervision_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.8f, axis.state.pack_voltage_v);

    /* 再次 read 失败，物理量应保持上拍值 */
    g_read_fail = 1;
    bm_bms_supervision_step(&axis);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.8f, axis.state.telemetry.pack_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, axis.state.telemetry.pack_current_a);
}

/* ================================================================
 * 测试 4：故障触发 → derate_factor 降至 derate_target 附近
 *
 * derate_ramp.rate_per_s = 10.0, derate_target = 0.5, dt_s = 0.01
 * 降额距离 = 0.5；步数 = 0.5 / (10 * 0.01) = 5 步可达
 * 再多运行几步确保达到 target。
 * ================================================================ */
void test_bms_supervision_fault_triggers_derating(void) {
    bm_bms_supervision_axis_t axis;
    uint32_t i;

    build_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_init(&axis));

    /* 持续过压，多步后 derate_factor 应降至 target（0.5±0.05） */
    g_voltage_v = 4.5f;
    for (i = 0u; i < 20u; ++i) {
        bm_bms_supervision_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, axis.state.derating.state.derate_factor);
    /* 遥测 status 应含 DERATED 标志 */
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status & BM_BMS_SUP_TEL_DERATED);
}

/* ================================================================
 * 测试 5：故障触发→降额→恢复全路径
 *   (a) 过压触发故障，derate_factor 降至 0.5
 *   (b) 恢复正常电压，等待 recovery_time_s + 恢复斜坡
 *   (c) derate_factor 恢复到 1.0
 * ================================================================ */
void test_bms_supervision_full_fault_derating_recovery(void) {
    bm_bms_supervision_axis_t axis;
    uint32_t i;

    build_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_init(&axis));

    /* (a) 触发故障，足够步数到达降额目标 */
    g_voltage_v = 4.5f;
    for (i = 0u; i < 20u; ++i) {
        bm_bms_supervision_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, axis.state.derating.state.derate_factor);

    /* (b) 恢复正常电压 */
    g_voltage_v = 3.7f;
    bm_bms_supervision_step(&axis);
    /* 故障锁存应清除 */
    TEST_ASSERT_EQUAL(0, axis.state.derating.state.fault_latched);

    /* (c) 等待恢复计时 + 斜坡，恢复时间 0.04s + 斜坡 0.5/(10/s) = 0.09s
     *     @dt_s=0.01s 共需约 9 步；多运行 30 步确保完成 */
    for (i = 0u; i < 30u; ++i) {
        bm_bms_supervision_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, axis.state.derating.state.derate_factor);
    /* 遥测 status 不应含 DERATED 标志 */
    TEST_ASSERT_EQUAL(0u, axis.state.telemetry.status & BM_BMS_SUP_TEL_DERATED);
}

/* ================================================================
 * 测试 6：validate_config 边界拒绝
 * ================================================================ */
void test_bms_supervision_validate_rejects_null(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bms_supervision_validate_config(NULL));
}

void test_bms_supervision_validate_rejects_zero_dt(void) {
    bm_bms_supervision_axis_t axis;

    build_axis(&axis);
    axis.config.dt_s = 0.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_bms_supervision_validate_config(&axis.config));
}

void test_bms_supervision_validate_rejects_inverted_voltage(void) {
    bm_bms_supervision_axis_t axis;

    build_axis(&axis);
    axis.config.v_max_v = 2.0f;   /* < v_min_v */
    axis.config.v_min_v = 3.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_bms_supervision_validate_config(&axis.config));
}

/* ================================================================
 * 测试 7：exec_ops 生命周期
 * ================================================================ */
void test_bms_supervision_exec_ops_lifecycle(void) {
    bm_bms_supervision_axis_t axis;
    bm_exec_t                 exec;

    build_axis(&axis);
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    /* init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_exec_ops.init(&exec));
    /* start 应返回 BM_OK */
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_exec_ops.start(&exec));

    /* safe_stop 应将 derate_factor 重置为 1.0 */
    axis.state.derating.state.derate_factor = 0.3f;
    bm_bms_supervision_exec_ops.safe_stop(&exec);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, axis.state.derating.state.derate_factor);
}

/* ================================================================
 * 测试 8：exec_ops init 对非法配置返回 BM_ERR_INVALID
 * ================================================================ */
void test_bms_supervision_exec_ops_init_rejects_bad_config(void) {
    bm_bms_supervision_axis_t axis;
    bm_exec_t                 exec;

    build_axis(&axis);
    axis.config.dt_s = -1.0f; /* 非法 */
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_bms_supervision_exec_ops.init(&exec));
}

/* ================================================================
 * 测试 9：exec_ops run 正确转发给 step（sequence 递增）
 * ================================================================ */
void test_bms_supervision_exec_ops_run_forwards_to_step(void) {
    bm_bms_supervision_axis_t axis;
    bm_exec_t                 exec;

    build_axis(&axis);
    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_exec_ops.init(&exec));
    bm_bms_supervision_exec_run(&exec);

    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.step_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bms_supervision_clears_fault_latch_when_normal);
    RUN_TEST(test_bms_supervision_read_fail_marks_stale);
    RUN_TEST(test_bms_supervision_read_fail_preserves_last_values);
    RUN_TEST(test_bms_supervision_fault_triggers_derating);
    RUN_TEST(test_bms_supervision_full_fault_derating_recovery);
    RUN_TEST(test_bms_supervision_validate_rejects_null);
    RUN_TEST(test_bms_supervision_validate_rejects_zero_dt);
    RUN_TEST(test_bms_supervision_validate_rejects_inverted_voltage);
    RUN_TEST(test_bms_supervision_exec_ops_lifecycle);
    RUN_TEST(test_bms_supervision_exec_ops_init_rejects_bad_config);
    RUN_TEST(test_bms_supervision_exec_ops_run_forwards_to_step);
    return UNITY_END();
}

/**
 * @file test_power_converter.c
 * @brief power_converter 组件单元测试
 *
 * 覆盖电流跟踪 happy-path、故障锁存路径与 validate_config 边界。
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
#include "bm/component/power_converter.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 测试用模拟资源 ---------- */

/* 模拟电流传感器 */
static float    g_plant_current;
static int      g_read_current_fail;  /* 非零时 read_current 返回错误 */
static float    g_duty_written;
static int      g_write_duty_fail;    /* 非零时 write_duty 返回错误 */
static uint32_t g_tel_count;
static uint32_t g_last_tel_status;

static int read_current_cb(void *user, float *i_out_a) {
    (void)user;
    if (g_read_current_fail) { return -1; }
    *i_out_a = g_plant_current;
    return 0;
}

static int write_duty_cb(void *user, float duty) {
    (void)user;
    if (g_write_duty_fail) { return -1; }
    /* 极简一阶惯性：模拟电流随占空比变化（增益 = 10 A/duty，τ ≈ 0.001s） */
    g_plant_current += (duty * 10.0f - g_plant_current) *
                       (0.001f / 0.005f);
    g_duty_written = duty;
    return 0;
}

static void publish_tel_cb(void *user,
                           const bm_pwr_conv_telemetry_t *telemetry) {
    (void)user;
    g_tel_count++;
    g_last_tel_status = telemetry->status;
}

/* ---------- axis 构造辅助 ---------- */
static void make_axis(bm_power_converter_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.pi_current.kp             = 0.5f;
    axis->config.pi_current.ki             = 20.0f;
    axis->config.pi_current.out_min        = 0.0f;
    axis->config.pi_current.out_max        = 1.0f;
    axis->config.pi_current.integrator_min = -5.0f;
    axis->config.pi_current.integrator_max =  5.0f;
    axis->config.i_ramp.rate_per_s         = 50.0f;
    axis->config.duty_min                  = 0.0f;
    axis->config.duty_max                  = 1.0f;
    axis->config.current_dt_s              = 0.001f;
    axis->resources.read_current           = read_current_cb;
    axis->resources.read_current_user      = NULL;
    axis->resources.write_duty             = write_duty_cb;
    axis->resources.write_duty_user        = NULL;
    axis->resources.publish_telemetry      = publish_tel_cb;
    axis->resources.publish_telemetry_user = NULL;
}

void setUp(void) {
    g_plant_current    = 0.0f;
    g_read_current_fail = 0;
    g_duty_written     = 0.0f;
    g_write_duty_fail  = 0;
    g_tel_count        = 0u;
    g_last_tel_status  = 0u;
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

/**
 * @brief validate_config 正常参数返回 BM_OK
 */
void test_power_converter_validate_config_ok(void) {
    bm_power_converter_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.current_dt_s = 0.001f;
    cfg.duty_min     = 0.0f;
    cfg.duty_max     = 1.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_power_converter_validate_config(&cfg));
}

/**
 * @brief validate_config NULL 返回 BM_ERR_INVALID
 */
void test_power_converter_validate_config_null(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_power_converter_validate_config(NULL));
}

/**
 * @brief validate_config dt_s <= 0 返回 BM_ERR_INVALID
 */
void test_power_converter_validate_config_bad_dt(void) {
    bm_power_converter_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.current_dt_s = 0.0f;
    cfg.duty_min     = 0.0f;
    cfg.duty_max     = 1.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_power_converter_validate_config(&cfg));
}

/**
 * @brief validate_config duty_max <= duty_min 返回 BM_ERR_INVALID
 */
void test_power_converter_validate_config_bad_duty_range(void) {
    bm_power_converter_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.current_dt_s = 0.001f;
    cfg.duty_min     = 0.5f;
    cfg.duty_max     = 0.5f; /* 不合法 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_power_converter_validate_config(&cfg));
}

/**
 * @brief 电流跟踪 happy-path：使能后多步运行，实际电流应向目标收敛
 */
void test_power_converter_current_tracks_setpoint(void) {
    bm_power_converter_axis_t axis;
    bm_pwr_conv_cmd_t cmd;
    uint32_t i;

    make_axis(&axis);
    bm_power_converter_reset(&axis);

    cmd.sequence = 1u;
    cmd.status   = BM_PWR_CONV_CMD_ENABLED;
    cmd.i_set_a  = 2.0f;
    bm_power_converter_apply_command(&axis, &cmd);

    for (i = 0u; i < 200u; i++) {
        bm_power_converter_current_step(&axis);
    }

    /* 电流应收敛到 setpoint 附近（允许 ±20% 误差） */
    TEST_ASSERT_FLOAT_WITHIN(0.4f, 2.0f, g_plant_current);
    /* 遥测应被发布 */
    TEST_ASSERT_TRUE(g_tel_count > 0u);
    /* 无故障 */
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    TEST_ASSERT_EQUAL(0u, g_last_tel_status & BM_PWR_CONV_TEL_FAULT);
}

/**
 * @brief 禁用（CMD_ENABLED 清零）时，duty 回到 duty_min，i_ref_a 清零
 */
void test_power_converter_disabled_resets_to_min_duty(void) {
    bm_power_converter_axis_t axis;
    bm_pwr_conv_cmd_t cmd;

    make_axis(&axis);
    bm_power_converter_reset(&axis);

    /* 先使能 */
    cmd.sequence = 1u;
    cmd.status   = BM_PWR_CONV_CMD_ENABLED;
    cmd.i_set_a  = 2.0f;
    bm_power_converter_apply_command(&axis, &cmd);
    bm_power_converter_current_step(&axis);

    /* 再禁用 */
    cmd.status  = 0u;
    cmd.i_set_a = 0.0f;
    bm_power_converter_apply_command(&axis, &cmd);
    bm_power_converter_current_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(1e-5f, axis.config.duty_min, axis.state.duty);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, axis.state.i_ref_a);
}

/**
 * @brief apply_command 带 BM_PWR_CONV_CMD_FAULT → 立即锁存故障，duty 回到 duty_min
 */
void test_power_converter_cmd_fault_latches(void) {
    bm_power_converter_axis_t axis;
    bm_pwr_conv_cmd_t cmd;

    make_axis(&axis);
    bm_power_converter_reset(&axis);

    cmd.sequence = 1u;
    cmd.status   = BM_PWR_CONV_CMD_ENABLED | BM_PWR_CONV_CMD_FAULT;
    cmd.i_set_a  = 3.0f;
    bm_power_converter_apply_command(&axis, &cmd);

    /* apply_command 本身就会锁存 */
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, axis.config.duty_min, axis.state.duty);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, axis.state.i_ref_a);
}

/**
 * @brief 锁存故障后 current_step 输出 FAULT 遥测，不更新 duty
 */
void test_power_converter_fault_latched_step_emits_fault_tel(void) {
    bm_power_converter_axis_t axis;
    bm_pwr_conv_cmd_t cmd;

    make_axis(&axis);
    bm_power_converter_reset(&axis);

    cmd.sequence = 1u;
    cmd.status   = BM_PWR_CONV_CMD_ENABLED | BM_PWR_CONV_CMD_FAULT;
    cmd.i_set_a  = 0.0f;
    bm_power_converter_apply_command(&axis, &cmd);

    bm_power_converter_current_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u, g_last_tel_status & BM_PWR_CONV_TEL_FAULT);
}

/**
 * @brief read_current 失败 → 触发故障锁存，遥测携带 FAULT 位
 */
void test_power_converter_read_current_failure_latches_fault(void) {
    bm_power_converter_axis_t axis;
    bm_pwr_conv_cmd_t cmd;

    make_axis(&axis);
    bm_power_converter_reset(&axis);

    cmd.sequence = 1u;
    cmd.status   = BM_PWR_CONV_CMD_ENABLED;
    cmd.i_set_a  = 2.0f;
    bm_power_converter_apply_command(&axis, &cmd);

    g_read_current_fail = 1; /* 模拟传感器故障 */
    bm_power_converter_current_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_NOT_EQUAL(0u, g_last_tel_status & BM_PWR_CONV_TEL_FAULT);
}

/**
 * @brief clear_fault 清除锁存，再次 step 可正常运行
 */
void test_power_converter_clear_fault_resumes_operation(void) {
    bm_power_converter_axis_t axis;
    bm_pwr_conv_cmd_t cmd;
    uint32_t tel_before;

    make_axis(&axis);
    bm_power_converter_reset(&axis);

    cmd.sequence = 1u;
    cmd.status   = BM_PWR_CONV_CMD_ENABLED | BM_PWR_CONV_CMD_FAULT;
    cmd.i_set_a  = 0.0f;
    bm_power_converter_apply_command(&axis, &cmd);
    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);

    bm_power_converter_clear_fault(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);

    cmd.status  = BM_PWR_CONV_CMD_ENABLED;
    cmd.i_set_a = 1.0f;
    bm_power_converter_apply_command(&axis, &cmd);

    tel_before = g_tel_count;
    bm_power_converter_current_step(&axis);
    TEST_ASSERT_TRUE(g_tel_count > tel_before);
    TEST_ASSERT_EQUAL(0u, g_last_tel_status & BM_PWR_CONV_TEL_FAULT);
}

/**
 * @brief clear_fault 对未锁存 axis 静默返回（不崩溃，状态不变）
 */
void test_power_converter_clear_fault_noop_when_not_latched(void) {
    bm_power_converter_axis_t axis;
    make_axis(&axis);
    bm_power_converter_reset(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    bm_power_converter_clear_fault(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
}

/**
 * @brief exec_ops init/start/safe_stop NULL 实例不崩溃
 */
void test_power_converter_exec_ops_null_safe(void) {
    bm_power_converter_exec_init(NULL);
    bm_power_converter_exec_start(NULL);
    bm_power_converter_exec_safe_stop(NULL);
    TEST_PASS();
}

/**
 * @brief reset/apply_command/current_step 传 NULL 不崩溃
 */
void test_power_converter_api_null_safe(void) {
    bm_power_converter_reset(NULL);
    bm_power_converter_apply_command(NULL, NULL);
    bm_power_converter_current_step(NULL);
    bm_power_converter_clear_fault(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_power_converter_validate_config_ok);
    RUN_TEST(test_power_converter_validate_config_null);
    RUN_TEST(test_power_converter_validate_config_bad_dt);
    RUN_TEST(test_power_converter_validate_config_bad_duty_range);
    RUN_TEST(test_power_converter_current_tracks_setpoint);
    RUN_TEST(test_power_converter_disabled_resets_to_min_duty);
    RUN_TEST(test_power_converter_cmd_fault_latches);
    RUN_TEST(test_power_converter_fault_latched_step_emits_fault_tel);
    RUN_TEST(test_power_converter_read_current_failure_latches_fault);
    RUN_TEST(test_power_converter_clear_fault_resumes_operation);
    RUN_TEST(test_power_converter_clear_fault_noop_when_not_latched);
    RUN_TEST(test_power_converter_exec_ops_null_safe);
    RUN_TEST(test_power_converter_api_null_safe);
    return UNITY_END();
}

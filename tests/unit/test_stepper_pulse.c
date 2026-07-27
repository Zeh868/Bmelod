/**
 * @file test_stepper_pulse.c
 * @brief stepper_pulse 组件单元测试（假回调验证步进计数/方向/限速/NULL 边界）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/stepper_pulse.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 假平台回调 ---------- */
static uint32_t g_step_high_count;
static uint32_t g_step_low_count;
static int      g_dir_level;
static int      g_dir_set_count;
static uint32_t g_last_interval;
static int      g_arm_count;

static void fake_step_high(void *user) { (void)user; g_step_high_count++; }
static void fake_step_low(void *user)  { (void)user; g_step_low_count++; }
static void fake_dir_set(void *user, int level) {
    (void)user;
    g_dir_level = level;
    g_dir_set_count++;
}
static int fake_arm_timer(void *user, uint32_t interval_us) {
    (void)user;
    g_last_interval = interval_us;
    g_arm_count++;
    return BM_OK;
}

static void make_axis(bm_stepper_pulse_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.max_step_rate_hz = 10000u;
    axis->config.dir_setup_us     = 20u;
    axis->resources.step_high = fake_step_high;
    axis->resources.step_low  = fake_step_low;
    axis->resources.dir_set   = fake_dir_set;
    axis->resources.arm_timer = fake_arm_timer;
    axis->resources.user      = NULL;
}

void setUp(void) {
    g_step_high_count = 0u;
    g_step_low_count  = 0u;
    g_dir_level       = -1;
    g_dir_set_count   = 0;
    g_last_interval   = 0u;
    g_arm_count       = 0;
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

void test_stepper_pulse_init_ok(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));
    TEST_ASSERT_EQUAL(0, axis.state.position);
    TEST_ASSERT_EQUAL(0u, axis.state.running);
    TEST_ASSERT_EQUAL_UINT32(1u, g_step_low_count); /* reset 拉低 STEP */
}

void test_stepper_pulse_init_invalid(void) {
    bm_stepper_pulse_axis_t axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_stepper_pulse_init(NULL));
    make_axis(&axis);
    axis.config.max_step_rate_hz = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_stepper_pulse_init(&axis));
    make_axis(&axis);
    axis.resources.arm_timer = NULL;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_stepper_pulse_init(&axis));
}

/**
 * @brief 1000 steps/s → 半周期 500µs；两个 on_timer 一拍完整脉冲，位置 +1
 */
void test_stepper_pulse_forward_counting(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    TEST_ASSERT_EQUAL(1u, axis.state.running);
    TEST_ASSERT_EQUAL(1, axis.state.dir);
    TEST_ASSERT_EQUAL(1, g_dir_level); /* 静止启动先给 DIR 电平（正向=1） */
    TEST_ASSERT_EQUAL_UINT32(500u, g_last_interval);

    /* 第 1 拍：方向建立槽（不发脉冲，武装 dir_setup_us） */
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL_UINT32(0u, g_step_high_count);
    TEST_ASSERT_EQUAL_UINT32(20u, g_last_interval);

    /* 第 2 拍：上升沿，位置 +1 */
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL_UINT32(1u, g_step_high_count);
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));
    TEST_ASSERT_EQUAL_UINT32(500u, g_last_interval);

    /* 第 3 拍：下降沿，位置不变（step_low 含 reset 一次共 2 次） */
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL_UINT32(2u, g_step_low_count);
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));
}

/**
 * @brief 反向：DIR 电平翻 0，建立槽后脉冲使位置递减
 */
void test_stepper_pulse_reverse_direction(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    bm_stepper_pulse_on_timer(&axis); /* 建立槽 */
    bm_stepper_pulse_on_timer(&axis); /* +1 */
    bm_stepper_pulse_on_timer(&axis); /* 下降沿 */
    bm_stepper_pulse_on_timer(&axis); /* +1 → position=2 */
    TEST_ASSERT_EQUAL(2, bm_stepper_pulse_position(&axis));

    bm_stepper_pulse_set_velocity(&axis, -1000.0f);
    TEST_ASSERT_EQUAL(-1, axis.state.dir);
    TEST_ASSERT_EQUAL(0, g_dir_level); /* DIR 翻转到反向电平 */

    bm_stepper_pulse_on_timer(&axis); /* 方向建立槽（STEP 停在高电平） */
    TEST_ASSERT_EQUAL(2, bm_stepper_pulse_position(&axis));
    bm_stepper_pulse_on_timer(&axis); /* 下降沿，不计步 */
    TEST_ASSERT_EQUAL(2, bm_stepper_pulse_position(&axis));
    bm_stepper_pulse_on_timer(&axis); /* 上升沿 → position=1 */
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));
}

/**
 * @brief 超速钳制：|v| > max_step_rate_hz → 钳到上限，半周期 ≥ 下限
 */
void test_stepper_pulse_rate_clamp(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 100000.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10000.0f, axis.state.velocity_sps);
    bm_stepper_pulse_on_timer(&axis); /* 建立槽 */
    bm_stepper_pulse_on_timer(&axis); /* 首脉冲 */
    /* max=10kHz → 半周期下限 50µs */
    TEST_ASSERT_TRUE(g_last_interval >= 50u);
}

/**
 * @brief stop：取消定时器、STEP 拉低、位置保持
 */
void test_stepper_pulse_stop_keeps_position(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    bm_stepper_pulse_on_timer(&axis);
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));

    bm_stepper_pulse_stop(&axis);
    TEST_ASSERT_EQUAL(0u, axis.state.running);
    TEST_ASSERT_EQUAL_UINT32(0u, g_last_interval); /* arm(0) 取消 */
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));

    /* 停止后 on_timer 不发脉冲 */
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));
}

/**
 * @brief NULL 安全：void 函数传 NULL 不崩溃
 */
void test_stepper_pulse_null_safety(void) {
    bm_stepper_pulse_set_velocity(NULL, 100.0f);
    bm_stepper_pulse_stop(NULL);
    bm_stepper_pulse_reset(NULL);
    bm_stepper_pulse_on_timer(NULL);
    TEST_ASSERT_EQUAL(0, bm_stepper_pulse_position(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stepper_pulse_init_ok);
    RUN_TEST(test_stepper_pulse_init_invalid);
    RUN_TEST(test_stepper_pulse_forward_counting);
    RUN_TEST(test_stepper_pulse_reverse_direction);
    RUN_TEST(test_stepper_pulse_rate_clamp);
    RUN_TEST(test_stepper_pulse_stop_keeps_position);
    RUN_TEST(test_stepper_pulse_null_safety);
    return UNITY_END();
}

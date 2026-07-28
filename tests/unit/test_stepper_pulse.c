/**
 * @file test_stepper_pulse.c
 * @brief stepper_pulse 组件单元测试（假回调验证步进计数/方向/限速/NULL 边界）
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            dir_hold/min 脉宽/GPIO fault/en_set
 * 2026-07-28       1.2            zeh            dir_hold 后 setup 仅等待一次
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
static int      g_en_level;
static int      g_en_set_count;
static uint32_t g_last_interval;
static int      g_arm_count;
static int      g_fail_step_high;
static int      g_fail_step_low;
static int      g_fail_dir_set;
static int      g_fail_en_set;
static int      g_fail_arm;

static int fake_step_high(void *user) {
    (void)user;
    if (g_fail_step_high) {
        return BM_ERR_IO;
    }
    g_step_high_count++;
    return BM_OK;
}
static int fake_step_low(void *user) {
    (void)user;
    if (g_fail_step_low) {
        return BM_ERR_IO;
    }
    g_step_low_count++;
    return BM_OK;
}
static int fake_dir_set(void *user, int level) {
    (void)user;
    if (g_fail_dir_set) {
        return BM_ERR_IO;
    }
    g_dir_level = level;
    g_dir_set_count++;
    return BM_OK;
}
static int fake_en_set(void *user, int level) {
    (void)user;
    if (g_fail_en_set) {
        return BM_ERR_IO;
    }
    g_en_level = level;
    g_en_set_count++;
    return BM_OK;
}
static int fake_arm_timer(void *user, uint32_t interval_us) {
    (void)user;
    if (g_fail_arm) {
        return BM_ERR_IO;
    }
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
    axis->resources.en_set    = fake_en_set;
    axis->resources.arm_timer = fake_arm_timer;
    axis->resources.user      = NULL;
}

void setUp(void) {
    g_step_high_count = 0u;
    g_step_low_count  = 0u;
    g_dir_level       = -1;
    g_dir_set_count   = 0;
    g_en_level        = -1;
    g_en_set_count    = 0;
    g_last_interval   = 0u;
    g_arm_count       = 0;
    g_fail_step_high  = 0;
    g_fail_step_low   = 0;
    g_fail_dir_set    = 0;
    g_fail_en_set     = 0;
    g_fail_arm        = 0;
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
    axis.config.min_high_us = 600u;
    axis.config.min_low_us  = 500u;
    axis.config.max_step_rate_hz = 1000u; /* 周期 1000µs < 1100µs min */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_stepper_pulse_validate_config(&axis.config));
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
 * @brief 反向：DIR 电平翻 0，建立槽后脉冲使位置递减；翻转时 STEP 先拉低
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
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.step_level); /* 翻转时 STEP 已拉低 */

    bm_stepper_pulse_on_timer(&axis); /* 方向建立槽 */
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
 * @brief min_high/min_low 抬升武装间隔
 */
void test_stepper_pulse_min_pulse_width(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    axis.config.max_step_rate_hz = 2000u; /* 周期 500µs，兼容 min 500µs */
    axis.config.min_high_us = 300u;
    axis.config.min_low_us  = 200u;
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 1000.0f); /* 半周期 500µs */
    bm_stepper_pulse_on_timer(&axis); /* 建立槽 */
    bm_stepper_pulse_on_timer(&axis); /* 上升沿 */
    TEST_ASSERT_EQUAL_UINT32(500u, g_last_interval); /* max(500,300)=500 */

    bm_stepper_pulse_on_timer(&axis); /* 下降沿 */
    TEST_ASSERT_EQUAL_UINT32(500u, g_last_interval); /* max(500,200)=500 */

    /* 更高速度使半周期低于 min_high（钳到 max 2000 steps/s → 半周期 250µs） */
    bm_stepper_pulse_set_velocity(&axis, 5000.0f);
    bm_stepper_pulse_on_timer(&axis); /* 上升沿 */
    TEST_ASSERT_EQUAL_UINT32(300u, g_last_interval); /* max(100,300)=300 */
}

/**
 * @brief dir_hold：运行中反向先拉低 STEP，再 hold，再 dir_set+setup
 */
void test_stepper_pulse_dir_hold(void) {
    bm_stepper_pulse_axis_t axis;
    int dir_before_hold;
    uint32_t step_high_before_reverse;

    make_axis(&axis);
    axis.config.dir_hold_us = 50u;
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    bm_stepper_pulse_on_timer(&axis); /* 建立槽 */
    bm_stepper_pulse_on_timer(&axis); /* +1 */
    bm_stepper_pulse_on_timer(&axis); /* 下降沿 */
    dir_before_hold = g_dir_level;
    step_high_before_reverse = g_step_high_count;

    g_dir_set_count = 0;
    bm_stepper_pulse_set_velocity(&axis, -1000.0f);
    TEST_ASSERT_EQUAL(1u, axis.state.dir_hold_pending);
    TEST_ASSERT_EQUAL(0, g_dir_set_count); /* hold 期间不改 DIR */
    TEST_ASSERT_EQUAL(dir_before_hold, g_dir_level);
    TEST_ASSERT_EQUAL_UINT32(50u, g_last_interval);

    bm_stepper_pulse_on_timer(&axis); /* hold 到期 → dir_set + arm setup */
    TEST_ASSERT_EQUAL(1, g_dir_set_count);
    TEST_ASSERT_EQUAL(0, g_dir_level);
    TEST_ASSERT_EQUAL_UINT32(20u, g_last_interval); /* dir_setup_us */
    TEST_ASSERT_EQUAL(0u, axis.state.dir_wait_pending);

    bm_stepper_pulse_on_timer(&axis); /* setup 到期 → 上升沿 → position=0 */
    TEST_ASSERT_EQUAL(0, bm_stepper_pulse_position(&axis));
    TEST_ASSERT_EQUAL_UINT32(step_high_before_reverse + 1u, g_step_high_count);
}

/**
 * @brief GPIO step_high 失败 → fault、停止、不再计步
 */
void test_stepper_pulse_gpio_fault_step_high(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    bm_stepper_pulse_on_timer(&axis); /* 建立槽 */
    g_fail_step_high = 1;
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL(1u, axis.state.fault);
    TEST_ASSERT_EQUAL(0u, axis.state.running);
    TEST_ASSERT_EQUAL(0, bm_stepper_pulse_position(&axis));

    g_fail_step_high = 0;
    bm_stepper_pulse_set_velocity(&axis, 1000.0f); /* fault 未清，应拒绝 */
    TEST_ASSERT_EQUAL(0, bm_stepper_pulse_position(&axis));

    bm_stepper_pulse_clear_fault(&axis);
    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    bm_stepper_pulse_on_timer(&axis);
    bm_stepper_pulse_on_timer(&axis);
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(&axis));
}

/**
 * @brief GPIO dir_set 失败 → fault、停止
 */
void test_stepper_pulse_gpio_fault_dir_set(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    g_fail_dir_set = 1;
    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    TEST_ASSERT_EQUAL(1u, axis.state.fault);
    TEST_ASSERT_EQUAL(0u, axis.state.running);
}

/**
 * @brief en_set 失败路径
 */
void test_stepper_pulse_set_enable_fault(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_set_enable(&axis, 1));
    TEST_ASSERT_EQUAL(1, g_en_level);

    g_fail_en_set = 1;
    TEST_ASSERT_EQUAL(BM_ERR_IO, bm_stepper_pulse_set_enable(&axis, 0));
    TEST_ASSERT_EQUAL(1u, axis.state.fault);
}

void test_stepper_pulse_set_enable_not_supported(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    axis.resources.en_set = NULL;
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_SUPPORTED, bm_stepper_pulse_set_enable(&axis, 1));
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
 * @brief reset 清除 fault
 */
void test_stepper_pulse_reset_clears_fault(void) {
    bm_stepper_pulse_axis_t axis;
    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&axis));

    g_fail_dir_set = 1;
    bm_stepper_pulse_set_velocity(&axis, 1000.0f);
    TEST_ASSERT_EQUAL(1u, axis.state.fault);

    g_fail_dir_set = 0;
    bm_stepper_pulse_reset(&axis);
    TEST_ASSERT_EQUAL(0u, axis.state.fault);
}

/**
 * @brief NULL 安全：void 函数传 NULL 不崩溃
 */
void test_stepper_pulse_null_safety(void) {
    bm_stepper_pulse_set_velocity(NULL, 100.0f);
    bm_stepper_pulse_stop(NULL);
    bm_stepper_pulse_reset(NULL);
    bm_stepper_pulse_clear_fault(NULL);
    bm_stepper_pulse_on_timer(NULL);
    TEST_ASSERT_EQUAL(0, bm_stepper_pulse_position(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_stepper_pulse_set_enable(NULL, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stepper_pulse_init_ok);
    RUN_TEST(test_stepper_pulse_init_invalid);
    RUN_TEST(test_stepper_pulse_forward_counting);
    RUN_TEST(test_stepper_pulse_reverse_direction);
    RUN_TEST(test_stepper_pulse_rate_clamp);
    RUN_TEST(test_stepper_pulse_min_pulse_width);
    RUN_TEST(test_stepper_pulse_dir_hold);
    RUN_TEST(test_stepper_pulse_gpio_fault_step_high);
    RUN_TEST(test_stepper_pulse_gpio_fault_dir_set);
    RUN_TEST(test_stepper_pulse_set_enable_fault);
    RUN_TEST(test_stepper_pulse_set_enable_not_supported);
    RUN_TEST(test_stepper_pulse_stop_keeps_position);
    RUN_TEST(test_stepper_pulse_reset_clears_fault);
    RUN_TEST(test_stepper_pulse_null_safety);
    return UNITY_END();
}

/**
 * @file test_stepper_pulse_hrtimer_adapter.c
 * @brief stepper_pulse 与高精度 Timer 适配器单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增适配器单测
 * 2026-07-28       1.1            zeh            GPIO 回调改 int 返回；en_set 形参
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/stepper_pulse_hrtimer_adapter.h"
#include "bm_hal_hrtimer_native.h"
#include "bm/common/bm_types.h"

#include <stdio.h>
#include <string.h>

/* ---------- 假 GPIO 回调 ---------- */
static uint32_t g_step_high_count;
static uint32_t g_step_low_count;
static int      g_dir_level;

static int fake_step_high(void *user) {
    (void)user;
    g_step_high_count++;
    return BM_OK;
}

static int fake_step_low(void *user) {
    (void)user;
    g_step_low_count++;
    return BM_OK;
}

static int fake_dir_set(void *user, int level) {
    (void)user;
    g_dir_level = level;
    return BM_OK;
}

static void make_config(bm_stepper_pulse_config_t *config) {
    (void)memset(config, 0, sizeof(*config));
    config->max_step_rate_hz = 10000u;
    config->dir_setup_us     = 20u;
}

void setUp(void) {
    bm_hal_hrtimer_native_reset();
    g_step_high_count = 0u;
    g_step_low_count  = 0u;
    g_dir_level       = -1;
}

void tearDown(void) {}

void test_adapter_init_links_timer_and_axis(void) {
    bm_stepper_pulse_hrtimer_adapter_t adapter;
    bm_stepper_pulse_config_t config;
    bm_stepper_pulse_axis_t *axis;

    make_config(&config);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_stepper_pulse_hrtimer_adapter_init(&adapter, &config,
                                              &bm_native_hrtimer0,
                                              fake_step_high, fake_step_low,
                                              fake_dir_set, NULL, NULL));

    axis = bm_stepper_pulse_hrtimer_adapter_axis(&adapter);
    TEST_ASSERT_NOT_NULL(axis);
    TEST_ASSERT_EQUAL(0, axis->state.position);
}

void test_adapter_velocity_triggers_timer(void) {
    bm_stepper_pulse_hrtimer_adapter_t adapter;
    bm_stepper_pulse_config_t config;
    bm_stepper_pulse_axis_t *axis;

    make_config(&config);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_stepper_pulse_hrtimer_adapter_init(&adapter, &config,
                                              &bm_native_hrtimer0,
                                              fake_step_high, fake_step_low,
                                              fake_dir_set, NULL, NULL));
    axis = bm_stepper_pulse_hrtimer_adapter_axis(&adapter);

    bm_stepper_pulse_set_velocity(axis, 1000.0f);
    TEST_ASSERT_EQUAL(1, g_dir_level);

    /* 推进时间触发方向建立槽 */
    bm_hal_hrtimer_native_advance_us(500u);
    printf("after 500us: high=%u low=%u pos=%ld\n",
           g_step_high_count, g_step_low_count,
           (long)bm_stepper_pulse_position(axis));
    TEST_ASSERT_EQUAL_UINT32(0u, g_step_high_count);

    /* 推进 20 µs 触发第一步 */
    bm_hal_hrtimer_native_advance_us(20u);
    printf("after 520us: high=%u low=%u pos=%ld\n",
           g_step_high_count, g_step_low_count,
           (long)bm_stepper_pulse_position(axis));
    TEST_ASSERT_EQUAL_UINT32(1u, g_step_high_count);
    TEST_ASSERT_EQUAL(1, bm_stepper_pulse_position(axis));

    /* 再推进 500 µs 触发下降沿 */
    bm_hal_hrtimer_native_advance_us(500u);
    TEST_ASSERT_EQUAL_UINT32(2u, g_step_low_count);
}

void test_adapter_stop_cancels_timer(void) {
    bm_stepper_pulse_hrtimer_adapter_t adapter;
    bm_stepper_pulse_config_t config;
    bm_stepper_pulse_axis_t *axis;

    make_config(&config);
    TEST_ASSERT_EQUAL(BM_OK,
        bm_stepper_pulse_hrtimer_adapter_init(&adapter, &config,
                                              &bm_native_hrtimer0,
                                              fake_step_high, fake_step_low,
                                              fake_dir_set, NULL, NULL));
    axis = bm_stepper_pulse_hrtimer_adapter_axis(&adapter);

    bm_stepper_pulse_set_velocity(axis, 1000.0f);
    bm_stepper_pulse_stop(axis);

    /* 停止后即使推进时间也不应再发脉冲 */
    bm_hal_hrtimer_native_advance_us(2000u);
    TEST_ASSERT_EQUAL_UINT32(1u, g_step_low_count); /* init/reset 时的一次拉低 */
    TEST_ASSERT_EQUAL_UINT32(0u, g_step_high_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_adapter_init_links_timer_and_axis);
    RUN_TEST(test_adapter_velocity_triggers_timer);
    RUN_TEST(test_adapter_stop_cancels_timer);
    return UNITY_END();
}

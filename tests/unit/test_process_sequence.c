/**
 * @file test_process_sequence.c
 * @brief process_sequence 组件单元测试
 *
 * 覆盖 TON/TOF 定时器、顺序步进与联锁超时。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 */
#include "unity.h"
#include "bm/component/process_sequence.h"
#include "bm/common/bm_types.h"

#include <string.h>

static int g_interlock_step;
static int g_interlock_allow;

static int interlock_ok(void *user, uint32_t step_index) {
    (void)user;
    if (!g_interlock_allow) {
        return 0;
    }
    return (step_index == g_interlock_step) ? 1 : 0;
}

void setUp(void) {
    g_interlock_step = 0u;
    g_interlock_allow = 0;
}

void tearDown(void) {}

void test_process_ton_delays_output(void) {
    bm_process_ton_state_t ton;

    bm_process_ton_reset(&ton, 3u);
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(&ton, 1));
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(&ton, 1));
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(&ton, 1));
    TEST_ASSERT_EQUAL(1, bm_process_ton_step(&ton, 1));
}

void test_process_tof_holds_output(void) {
    bm_process_tof_state_t tof;

    bm_process_tof_reset(&tof, 2u);
    TEST_ASSERT_EQUAL(1, bm_process_tof_step(&tof, 1));
    TEST_ASSERT_EQUAL(1, bm_process_tof_step(&tof, 0));
    TEST_ASSERT_EQUAL(1, bm_process_tof_step(&tof, 0));
    TEST_ASSERT_EQUAL(0, bm_process_tof_step(&tof, 0));
}

void test_process_sequence_advances_on_timeout(void) {
    bm_process_sequence_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.step_count = 2u;
    axis.config.dt_s = 0.1f;
    axis.config.steps[0].timeout_s = 0.2f;
    axis.config.steps[1].timeout_s = 0.1f;
    TEST_ASSERT_EQUAL(BM_OK, bm_process_sequence_validate_config(&axis.config));
    bm_process_sequence_reset(&axis);
    bm_process_sequence_start(&axis);

    for (i = 0u; i < 10u; ++i) {
        bm_process_sequence_step(&axis, NULL, NULL);
    }

    TEST_ASSERT_TRUE(axis.state.done != 0);
    TEST_ASSERT_EQUAL_UINT32(2u, axis.state.current_step);
}

void test_process_sequence_waits_for_interlock(void) {
    bm_process_sequence_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.step_count = 1u;
    axis.config.dt_s = 0.1f;
    axis.config.steps[0].timeout_s = 0.05f;
    g_interlock_step = 0u;
    bm_process_sequence_reset(&axis);
    bm_process_sequence_start(&axis);

    for (i = 0u; i < 3u; ++i) {
        bm_process_sequence_step(&axis, interlock_ok, NULL);
    }
    TEST_ASSERT_EQUAL(0, axis.state.done);

    g_interlock_allow = 1;
    g_interlock_step = 0u;
    bm_process_sequence_step(&axis, interlock_ok, NULL);
    TEST_ASSERT_TRUE(axis.state.done != 0);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_process_ton_delays_output);
    RUN_TEST(test_process_tof_holds_output);
    RUN_TEST(test_process_sequence_advances_on_timeout);
    RUN_TEST(test_process_sequence_waits_for_interlock);
    return UNITY_END();
}

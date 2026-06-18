/**
 * @file test_bms_supervision.c
 * @brief bms_supervision 组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */
#include "unity.h"
#include "bm/common/bm_types.h"
#include "bm/component/bms_supervision.h"

#include <string.h>

static float g_voltage_v;
static float g_current_a;
static float g_temp_c;

static int read_sample(void *user,
                       float *voltage_v,
                       float *current_a,
                       float *temp_c) {
    (void)user;
    *voltage_v = g_voltage_v;
    *current_a = g_current_a;
    *temp_c = g_temp_c;
    return 0;
}

void setUp(void) {
    g_voltage_v = 3.7f;
    g_current_a = 0.0f;
    g_temp_c = 25.0f;
}

void tearDown(void) {
}

void test_bms_supervision_clears_fault_latch_when_normal(void) {
    bm_bms_supervision_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.v_max_v = 4.2f;
    axis.config.v_min_v = 2.5f;
    axis.config.i_max_a = 100.0f;
    axis.config.temp_max_c = 60.0f;
    axis.config.dt_s = 0.01f;
    axis.config.derate_ramp.rate_per_s = 10.0f;
    axis.config.recovery_time_s = 0.04f;
    axis.config.derate_target = 0.5f;
    axis.resources.read_sample = read_sample;

    TEST_ASSERT_EQUAL(BM_OK, bm_bms_supervision_init(&axis));

    g_voltage_v = 4.5f;
    bm_bms_supervision_step(&axis);
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.limit_flags);
    TEST_ASSERT_EQUAL(1, axis.state.derating.state.fault_latched);

    g_voltage_v = 3.7f;
    bm_bms_supervision_step(&axis);
    TEST_ASSERT_EQUAL(0u, axis.state.limit_flags);
    TEST_ASSERT_EQUAL(0, axis.state.derating.state.fault_latched);

    for (i = 0u; i < 50u; ++i) {
        bm_bms_supervision_step(&axis);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, axis.state.derating.state.derate_factor);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bms_supervision_clears_fault_latch_when_normal);
    return UNITY_END();
}

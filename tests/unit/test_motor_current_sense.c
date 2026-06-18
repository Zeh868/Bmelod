/**
 * @file test_motor_current_sense.c
 * @brief motor_current_sense 采样窗口单元测试
 *
 * 覆盖 PWM 扇区采样窗口判定与电流重构有效标志。
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
#include "bm/component/motor_current_sense.h"
#include "bm/common/bm_types.h"

#include <string.h>

static float g_ia;
static float g_ib;

void setUp(void) {
    g_ia = 1.0f;
    g_ib = -0.5f;
}

void tearDown(void) {}

void test_motor_current_sense_sample_window(void) {
    bm_motor_current_sense_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.topology = BM_MOTOR_CS_2SHUNT;
    axis.config.pwm_sector = 0u;
    axis.config.adc_phase_deg = 30.0f;
    axis.config.sample_window_deg = 20.0f;
    axis.resources.sim_fb.ia_a = &g_ia;
    axis.resources.sim_fb.ib_a = &g_ib;

    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_init(&axis));
    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_step(&axis));
    TEST_ASSERT_EQUAL(1, axis.state.sample_valid);
    TEST_ASSERT_EQUAL(1, axis.state.valid);

    axis.config.adc_phase_deg = 200.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_motor_current_sense_step(&axis));
    TEST_ASSERT_EQUAL(0, axis.state.sample_valid);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motor_current_sense_sample_window);
    return UNITY_END();
}

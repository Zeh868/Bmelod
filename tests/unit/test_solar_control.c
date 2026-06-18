/**
 * @file test_solar_control.c
 * @brief solar_control 组件单元测试
 *
 * 覆盖 MPPT 步进、限功率降额与遥测发布。
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
#include "bm/component/solar_control.h"
#include "bm/common/bm_types.h"

#include <string.h>

static float g_voltage;
static float g_current;
static float g_vref_out;

static int read_iv(void *user, float *voltage_v, float *current_a) {
    (void)user;
    *voltage_v = g_voltage;
    *current_a = g_current;
    return 0;
}

static int write_vref(void *user, float v_ref_v) {
    (void)user;
    g_vref_out = v_ref_v;
    return 0;
}

void setUp(void) {
    g_voltage = 18.0f;
    g_current = 2.0f;
    g_vref_out = 0.0f;
}

void tearDown(void) {}

void test_solar_control_mppt_and_limit(void) {
    bm_solar_control_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.mppt_mode = BM_SOLAR_MPPT_PO;
    axis.config.mppt_po.step_v = 0.1f;
    axis.config.mppt_po.v_min = 10.0f;
    axis.config.mppt_po.v_max = 24.0f;
    axis.config.power_limit_w = 30.0f;
    axis.config.v_init_v = 17.0f;
    axis.resources.read_iv = read_iv;
    axis.resources.write_vref = write_vref;

    TEST_ASSERT_EQUAL(BM_OK, bm_solar_control_init(&axis));
    bm_solar_control_step(&axis);
    TEST_ASSERT_TRUE(g_vref_out > 0.0f);
    TEST_ASSERT_TRUE(axis.state.telemetry.power_w > axis.config.power_limit_w);
    TEST_ASSERT_NOT_EQUAL(0u, axis.state.telemetry.status &
                          BM_SOLAR_CTRL_TEL_LIMITED);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_solar_control_mppt_and_limit);
    return UNITY_END();
}

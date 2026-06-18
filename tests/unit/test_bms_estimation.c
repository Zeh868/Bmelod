/**
 * @file test_bms_estimation.c
 * @brief bms_estimation 组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm/component/bms_estimation.h"

#include <string.h>
static float g_sim_current_a;
static float g_sim_voltage_v;
static float g_sim_temp_c;
static uint32_t g_tel_count;

static const float s_soc_lut[] = { 0.0f, 0.5f, 1.0f };
static const float s_ocv_lut[] = { 3.0f, 3.7f, 4.2f };
static const bm_algo_ocv_table_t s_ocv_table = {
    s_soc_lut, s_ocv_lut, 3u
};

static int read_sample(void *user,
                       float *pack_current_a,
                       float *pack_voltage_v,
                       float *temp_c) {
    (void)user;
    *pack_current_a = g_sim_current_a;
    *pack_voltage_v = g_sim_voltage_v;
    *temp_c = g_sim_temp_c;
    return 0;
}

static void publish_tel(void *user, const bm_bms_est_telemetry_t *telemetry) {
    (void)user;
    (void)telemetry;
    g_tel_count++;
}

void setUp(void) {
    g_sim_current_a = 0.0f;
    g_sim_voltage_v = 3.7f;
    g_sim_temp_c = 25.0f;
    g_tel_count = 0u;
}

void tearDown(void) {
}

void test_bms_estimation_charge_increases_soc(void) {
    bm_bms_estimation_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.coulomb.nominal_capacity_ah = 1.0f;
    axis.config.coulomb.coulomb_efficiency = 1.0f;
    axis.config.coulomb.soc_min = 0.0f;
    axis.config.coulomb.soc_max = 1.0f;
    axis.config.temp.ref_temp_c = 25.0f;
    axis.config.temp.capacity_coeff_per_c = 0.0f;
    axis.config.temp.ocv_shift_v_per_c = 0.0f;
    axis.config.fusion.ocv_weight = 0.0f;
    axis.config.ocv_table = &s_ocv_table;
    axis.config.resting_current_a = 0.05f;
    axis.config.resting_time_s = 1.0f;
    axis.config.dt_s = 0.1f;
    axis.config.soc_init = 0.5f;
    axis.resources.read_sample = read_sample;
    axis.resources.publish_telemetry = publish_tel;

    g_sim_current_a = 10.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_estimation_validate_config(&axis.config));
    bm_bms_estimation_reset(&axis, 0.5f);

    for (i = 0u; i < 50u; ++i) {
        bm_bms_estimation_step(&axis);
    }

    TEST_ASSERT_TRUE(axis.state.soc_fused > 0.51f);
    TEST_ASSERT_TRUE(g_tel_count > 0u);
}

void test_bms_estimation_ekf_mode_step(void) {
    bm_bms_estimation_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode = BM_BMS_EST_MODE_EKF;
    axis.config.coulomb.nominal_capacity_ah = 10.0f;
    axis.config.coulomb.coulomb_efficiency = 1.0f;
    axis.config.coulomb.soc_min = 0.0f;
    axis.config.coulomb.soc_max = 1.0f;
    axis.config.temp.ref_temp_c = 25.0f;
    axis.config.ocv_table = &s_ocv_table;
    axis.config.ocv_slope_v_per_soc = 0.5f;
    axis.config.soc_ekf.q_soc = 1e-6f;
    axis.config.soc_ekf.q_bias = 1e-8f;
    axis.config.soc_ekf.r_v = 0.01f;
    axis.config.soc_ekf.coulomb_efficiency = 1.0f;
    axis.config.soc_ekf.nominal_capacity_ah = 10.0f;
    axis.config.dt_s = 1.0f;
    axis.config.soc_init = 0.5f;
    axis.resources.read_sample = read_sample;
    axis.resources.publish_telemetry = publish_tel;

    g_sim_current_a = 0.0f;
    g_sim_voltage_v = 3.7f;
    TEST_ASSERT_EQUAL(BM_OK, bm_bms_estimation_validate_config(&axis.config));
    bm_bms_estimation_reset(&axis, 0.5f);
    bm_bms_estimation_step(&axis);

    TEST_ASSERT_TRUE(axis.state.soc_fused >= 0.0f);
    TEST_ASSERT_TRUE(axis.state.soc_fused <= 1.0f);
    TEST_ASSERT_TRUE((axis.state.telemetry.status & BM_BMS_EST_TEL_EKF) != 0u);
    TEST_ASSERT_EQUAL(BM_BMS_EST_MODE_EKF, axis.state.telemetry.est_mode);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bms_estimation_charge_increases_soc);
    RUN_TEST(test_bms_estimation_ekf_mode_step);
    return UNITY_END();
}

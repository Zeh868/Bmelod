/**
 * @file test_algo_fusion.c
 * @brief bm_algorithm 估计融合域（EKF/UKF/IMU 标定/Mahony/SOC-EKF）单元测试
 *
 * 由 test_algorithm.c 按域拆分而来（架构改进计划任务 1.5b 项 6），纯移动、
 * 不改测试内容；测试用例总数与拆分前的 Unity 内部计数之和保持不变。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-02
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-02       1.0            zeh            自 test_algorithm.c 拆分
 *
 */

#include "unity.h"
#include "bm_algorithm.h"

#include <math.h>
#include <string.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

static void test_ekf_covariance_stays_symmetric(void) {
    bm_algo_ekf_cv_config_t cfg = {
        .q_pos = 0.01f,
        .q_vel = 0.01f,
        .r_pos = 0.1f
    };
    bm_algo_ekf_cv_state_t st;
    int i;

    bm_algo_ekf_cv_reset(&st, 0.0f, 0.0f);
    for (i = 0; i < 20; ++i) {
        bm_algo_ekf_cv_predict(&st, &cfg, 0.1f);
        bm_algo_ekf_cv_update(&st, &cfg, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, st.p01, st.p10);
}

static void test_ukf1d_identity_tracks_measurement(void) {
    bm_algo_ukf1d_config_t cfg = {
        .q = 0.01f,
        .r = 0.1f,
        .measurement_model = BM_ALGO_UKF1D_MEAS_IDENTITY
    };
    bm_algo_ukf1d_state_t st;
    int i;

    bm_algo_ukf1d_reset(&st, 0.0f, 1.0f);
    for (i = 0; i < 30; ++i) {
        bm_algo_ukf1d_predict(&st, &cfg);
        TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_OK,
                          bm_algo_ukf1d_update(&st, &cfg, 2.0f));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, st.x);
    TEST_ASSERT_TRUE(isfinite(st.p) && st.p >= 0.0f);
}

static void test_ukf1d_square_model_updates(void) {
    bm_algo_ukf1d_config_t cfg = {
        .q = 0.001f,
        .r = 0.05f,
        .measurement_model = BM_ALGO_UKF1D_MEAS_SQUARE
    };
    bm_algo_ukf1d_state_t st;

    bm_algo_ukf1d_reset(&st, 2.0f, 0.5f);
    bm_algo_ukf1d_predict(&st, &cfg);
    TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_OK,
                      bm_algo_ukf1d_update(&st, &cfg, 4.0f));
    TEST_ASSERT_TRUE(isfinite(st.x));
}

static void test_ekf_gate_and_gated_update(void) {
    bm_algo_ekf_cv_config_t cfg = {
        .q_pos = 0.01f,
        .q_vel = 0.01f,
        .r_pos = 0.1f
    };
    bm_algo_ekf_gate_config_t gate = { .innovation_threshold = 1.0f };
    bm_algo_ekf_cv_state_t st;
    float pos_before;

    TEST_ASSERT_EQUAL(1, bm_algo_ekf_gate_accept(0.1f, 1.0f, 9.0f));
    TEST_ASSERT_EQUAL(0, bm_algo_ekf_gate_accept(5.0f, 0.01f, 1.0f));

    bm_algo_ekf_cv_reset(&st, 0.0f, 0.0f);
    bm_algo_ekf_cv_predict(&st, &cfg, 0.1f);
    pos_before = st.pos;
    TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_GATED,
                      bm_algo_ekf_cv_update_gated(&st, &cfg, 100.0f, &gate));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, pos_before, st.pos);

    TEST_ASSERT_EQUAL(BM_ALGO_EKF_UPDATE_OK,
                      bm_algo_ekf_cv_update_gated(&st, &cfg, 0.2f, &gate));
    TEST_ASSERT_TRUE(fabsf(st.pos - pos_before) > 0.0f);
}

static void test_imu_calib_apply_and_accumulator(void) {
    bm_algo_imu_calib_config_t calib = {
        .gyro_bias = { 0.1f, 0.0f, 0.0f },
        .accel_bias = { 0.0f, 0.0f, 0.2f },
        .gyro_scale = { 1.0f, 1.0f, 1.0f },
        .accel_scale = { 1.0f, 1.0f, 1.0f }
    };
    bm_algo_imu_calib_accumulator_t acc;
    const float raw_g[3] = { 0.2f, 0.0f, 0.0f };
    const float raw_a[3] = { 0.0f, 0.0f, 9.91f };
    const float expect_g[3] = { 0.0f, 0.0f, 9.81f };
    float out_g[3];
    float out_a[3];
    int i;

    bm_algo_imu_calib_apply(&calib, raw_g, raw_a, out_g, out_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, out_g[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.71f, out_a[2]);

    bm_algo_imu_calib_accumulator_reset(&acc);
    for (i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL(0, bm_algo_imu_calib_accumulator_feed(&acc, raw_g, raw_a));
    }
    memset(&calib, 0, sizeof(calib));
    TEST_ASSERT_EQUAL(0, bm_algo_imu_calib_accumulator_finish(&acc, expect_g, &calib));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, calib.gyro_bias[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.1f, calib.accel_bias[2]);
}

static void test_mahony_uses_simultaneous_quaternion_update(void) {
    bm_algo_mahony_config_t cfg = { .kp = 0.0f, .ki = 0.0f };
    bm_algo_mahony_state_t st;
    float norm = sqrtf(1.14f);

    bm_algo_mahony_reset(&st);
    bm_algo_mahony_step(&st, &cfg, 1.0f, 2.0f, 3.0f,
                        0.0f, 0.0f, 0.0f, 0.2f);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f / norm, st.q.w);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1f / norm, st.q.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.2f / norm, st.q.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3f / norm, st.q.z);
}

static void test_soc_ekf_and_power_quality(void) {
    bm_algo_soc_ekf_config_t ekf_cfg = {
        .q_soc = 1e-6f,
        .q_bias = 1e-8f,
        .r_v = 0.01f,
        .coulomb_efficiency = 1.0f,
        .nominal_capacity_ah = 10.0f,
        .ocv_slope_v_per_soc = 0.5f
    };
    bm_algo_soc_ekf_state_t ekf;
    const float harmonics[3] = { 1.0f, 0.1f, 0.05f };
    float p;
    float q;
    float s;

    bm_algo_soc_ekf_reset(&ekf, 0.5f);
    bm_algo_soc_ekf_predict(&ekf, &ekf_cfg, 1.0f, 1.0f);
    bm_algo_soc_ekf_update_voltage(&ekf, &ekf_cfg, 3.7f, 3.65f);
    TEST_ASSERT_TRUE(ekf.soc >= 0.0f && ekf.soc <= 1.0f);
    TEST_ASSERT_TRUE(fabsf(ekf.p10) > 0.0f);
    TEST_ASSERT_TRUE(fabsf(ekf.bias_a) > 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.5f, 11.18f,
        bm_algo_thd_percent(harmonics, 3u));
    bm_algo_power_quality_pq(230.0f, 10.0f, 0.0f, &p, &q, &s);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2300.0f, p);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, q);
}

void test_algo_fusion(void) {
    RUN_TEST(test_ekf_covariance_stays_symmetric);
    RUN_TEST(test_ukf1d_identity_tracks_measurement);
    RUN_TEST(test_ukf1d_square_model_updates);
    RUN_TEST(test_ekf_gate_and_gated_update);
    RUN_TEST(test_imu_calib_apply_and_accumulator);
    RUN_TEST(test_mahony_uses_simultaneous_quaternion_update);
    RUN_TEST(test_soc_ekf_and_power_quality);
}

int main(void) {
    UNITY_BEGIN();
    test_algo_fusion();
    return UNITY_END();
}

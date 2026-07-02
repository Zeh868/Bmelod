/**
 * @file test_algo_motor.c
 * @brief bm_algorithm 电机域（Clarke/Park、电压标度死区、磁链观测器/MTPA）单元测试
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

static void test_clarke_park_roundtrip(void) {
    bm_algo_abc_t abc = { .ia = 1.0f, .ib = -0.5f, .ic = -0.5f };
    bm_algo_alphabeta_t ab;
    bm_algo_dq_t dq;
    bm_algo_alphabeta_t ab2;

    bm_algo_clarke(&abc, &ab);
    bm_algo_park(&ab, 0.5f, &dq);
    bm_algo_inv_park(&dq, 0.5f, &ab2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ab.i_alpha, ab2.i_alpha);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, ab.i_beta, ab2.i_beta);
}

static void test_motor_voltage_scaling_and_deadtime(void) {
    bm_algo_svpwm_out_t pwm;

    bm_algo_svpwm(6.0f, 0.0f, 24.0f, &pwm);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.6875f, pwm.duty_a);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3125f, pwm.duty_b);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.3125f, pwm.duty_c);

    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 3.2f,
        bm_algo_deadtime_comp_v_period(
            2.0f, 1.0f, 1e-6f, 20e-6f, 24.0f));
    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 2.0f,
        bm_algo_deadtime_comp_v(2.0f, 1.0f, 1e-6f, 24.0f));
}

static void test_flux_observer_and_mtpa(void) {
    bm_algo_flux_observer_config_t obs_cfg_ls = {
        .rs_ohm = 0.5f, .ls_h = 0.001f, .pll_kp = 10.0f, .pll_ki = 100.0f
    };
    bm_algo_flux_observer_config_t obs_cfg_no_ls = obs_cfg_ls;
    bm_algo_flux_observer_state_t obs_ls;
    bm_algo_flux_observer_state_t obs_no_ls;
    float theta_ls;
    float theta_no_ls;
    int i;

    obs_cfg_no_ls.ls_h = 0.0f;
    bm_algo_flux_observer_reset(&obs_ls, 0.0f);
    bm_algo_flux_observer_reset(&obs_no_ls, 0.0f);
    for (i = 0; i < 100; ++i) {
        float t = (float)i * 0.001f;
        float v_alpha = -sinf(t);
        float v_beta = cosf(t);
        float i_alpha = 0.5f * sinf(t);
        float i_beta = -0.5f * cosf(t);

        theta_ls = bm_algo_flux_observer_step(&obs_ls, &obs_cfg_ls,
                                              v_alpha, v_beta,
                                              i_alpha, i_beta, 0.001f);
        theta_no_ls = bm_algo_flux_observer_step(&obs_no_ls, &obs_cfg_no_ls,
                                                   v_alpha, v_beta,
                                                   i_alpha, i_beta, 0.001f);
        (void)theta_ls;
        (void)theta_no_ls;
    }
    TEST_ASSERT_TRUE(fabsf(obs_ls.omega_rad_s) > 0.1f);
    TEST_ASSERT_TRUE(fabsf(obs_ls.theta_rad - obs_no_ls.theta_rad) > 0.01f);
    TEST_ASSERT_TRUE(bm_algo_mtpa_id_ref(2.0f, 0.001f, 0.002f, 0.05f) < 0.0f);
    TEST_ASSERT_TRUE(
        fabsf(bm_algo_mtpa_id_ref(2.0f, 0.001f, 0.002f, 0.01f)) >
        fabsf(bm_algo_mtpa_id_ref(2.0f, 0.001f, 0.002f, 0.20f)));
}

void test_algo_motor(void) {
    RUN_TEST(test_clarke_park_roundtrip);
    RUN_TEST(test_motor_voltage_scaling_and_deadtime);
    RUN_TEST(test_flux_observer_and_mtpa);
}

int main(void) {
    UNITY_BEGIN();
    test_algo_motor();
    return UNITY_END();
}

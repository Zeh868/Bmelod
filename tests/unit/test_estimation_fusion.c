/**
 * @file test_estimation_fusion.c
 * @brief estimation_fusion 组件单元测试
 *
 * 覆盖 validate_config 放行逻辑、EKF_CV 融合模式 step 分支可用性
 * 及数值合理性（pitch 角收敛到加速度计测量值附近）、exec_ops 生命周期。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-23       1.0            zeh            首版：EKF_CV 模式最小单测
 * 2026-06-23       1.1            zeh            补 exec_ops 生命周期测试
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/estimation_fusion.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/* ---------- 测试桩数据 ---------- */

/** 模拟 IMU：固定 gx/gy/gz=0、ax=0/ay=0/az=1（重力沿 Z 轴，pitch≈0） */
static int s_read_imu_zero_pitch;   /**< 控制返回码（0 = 成功）*/
static float s_sim_gx;
static float s_sim_gy;
static float s_sim_gz;
static float s_sim_ax;
static float s_sim_ay;
static float s_sim_az;
static uint32_t s_tel_count;

static int read_imu_stub(void *user,
                         float *gx, float *gy, float *gz,
                         float *ax, float *ay, float *az) {
    (void)user;
    *gx = s_sim_gx;
    *gy = s_sim_gy;
    *gz = s_sim_gz;
    *ax = s_sim_ax;
    *ay = s_sim_ay;
    *az = s_sim_az;
    return s_read_imu_zero_pitch;
}

static void publish_tel_stub(void *user,
                             const bm_estimation_fusion_telemetry_t *tel) {
    (void)user;
    (void)tel;
    s_tel_count++;
}

/* ---------- setUp / tearDown ---------- */

void setUp(void) {
    s_read_imu_zero_pitch = 0;
    s_sim_gx = 0.0f;
    s_sim_gy = 0.0f;
    s_sim_gz = 0.0f;
    s_sim_ax = 0.0f;
    s_sim_ay = 0.0f;
    s_sim_az = 1.0f;   /* 重力沿 Z → pitch = atan2(-0, 1) = 0 */
    s_tel_count = 0u;
}

void tearDown(void) {}

/* ---------- 测试用例 ---------- */

/**
 * @brief validate_config 对 EKF_CV 模式合法参数应返回 BM_OK
 */
void test_ekf_cv_validate_ok(void) {
    bm_estimation_fusion_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode            = BM_EST_FUSION_EKF_CV;
    cfg.dt_s            = 0.01f;
    cfg.ekf_cv.q_pos    = 0.01f;
    cfg.ekf_cv.q_vel    = 0.01f;
    cfg.ekf_cv.r_pos    = 0.1f;

    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_validate_config(&cfg));
}

/**
 * @brief validate_config 对 EKF_CV 模式负噪声参数应返回 BM_ERR_INVALID
 */
void test_ekf_cv_validate_negative_noise_fails(void) {
    bm_estimation_fusion_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode            = BM_EST_FUSION_EKF_CV;
    cfg.dt_s            = 0.01f;
    cfg.ekf_cv.q_pos    = -0.01f;   /* 非法 */
    cfg.ekf_cv.q_vel    = 0.01f;
    cfg.ekf_cv.r_pos    = 0.1f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_estimation_fusion_validate_config(&cfg));
}

/**
 * @brief EKF_CV 模式 init 后 step 应能正常运行，遥测至少触发一次
 */
void test_ekf_cv_step_runs_and_publishes(void) {
    bm_estimation_fusion_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode            = BM_EST_FUSION_EKF_CV;
    axis.config.dt_s            = 0.01f;
    axis.config.ekf_cv.q_pos    = 0.01f;
    axis.config.ekf_cv.q_vel    = 0.01f;
    axis.config.ekf_cv.r_pos    = 0.1f;
    axis.resources.read_imu     = read_imu_stub;
    axis.resources.publish_telemetry = publish_tel_stub;

    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_init(&axis));

    bm_estimation_fusion_step(&axis);

    TEST_ASSERT_EQUAL_UINT32(1u, s_tel_count);
    TEST_ASSERT_TRUE(axis.state.telemetry.status == BM_EST_FUSION_TEL_VALID);
}

/**
 * @brief EKF_CV 模式：pitch 为 0 时，多步后输出应收敛到 0 附近（|pitch| < 0.05 rad）
 */
void test_ekf_cv_pitch_converges_to_zero(void) {
    bm_estimation_fusion_axis_t axis;
    uint32_t i;

    /* IMU 模拟：ax=0, az=1 → accel pitch ≈ 0; gy=0 */
    s_sim_ax = 0.0f;
    s_sim_az = 1.0f;
    s_sim_gy = 0.0f;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode            = BM_EST_FUSION_EKF_CV;
    axis.config.dt_s            = 0.01f;
    axis.config.ekf_cv.q_pos    = 0.001f;
    axis.config.ekf_cv.q_vel    = 0.001f;
    axis.config.ekf_cv.r_pos    = 0.05f;
    axis.resources.read_imu     = read_imu_stub;
    axis.resources.publish_telemetry = publish_tel_stub;

    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_init(&axis));

    for (i = 0u; i < 100u; ++i) {
        bm_estimation_fusion_step(&axis);
    }

    /* 经 100 步（1 s）收敛，pitch 应在 ±0.05 rad 以内 */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, axis.state.euler.pitch_rad);
    /* roll 与 yaw 固定为 0 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.euler.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.euler.yaw_rad);
}

/**
 * @brief EKF_CV 模式：恒定俯仰（pitch = +30°），
 *        输出应收敛到约 0.5236 rad（±0.05 rad 容差）
 *
 * 符号约定：pitch = atan2(-ax, az)，pitch > 0 时 ax < 0。
 * 令 ax = -sin(30°) ≈ -0.5，az = cos(30°) ≈ 0.866。
 */
void test_ekf_cv_pitch_converges_to_30deg(void) {
    bm_estimation_fusion_axis_t axis;
    uint32_t i;
    const float expected_pitch = 3.14159265f / 6.0f; /* π/6 ≈ 0.5236 rad */

    /* ax = -sin(30°)，符合 atan2(-ax, az) = atan2(0.5, 0.866) ≈ +0.5236 rad */
    s_sim_ax = -0.5f;
    s_sim_az = 0.8660254f;
    s_sim_gy = 0.0f;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode            = BM_EST_FUSION_EKF_CV;
    axis.config.dt_s            = 0.01f;
    axis.config.ekf_cv.q_pos    = 0.001f;
    axis.config.ekf_cv.q_vel    = 0.001f;
    axis.config.ekf_cv.r_pos    = 0.05f;
    axis.resources.read_imu     = read_imu_stub;
    axis.resources.publish_telemetry = publish_tel_stub;

    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_init(&axis));

    for (i = 0u; i < 200u; ++i) {
        bm_estimation_fusion_step(&axis);
    }

    TEST_ASSERT_FLOAT_WITHIN(0.05f, expected_pitch, axis.state.euler.pitch_rad);
}

/**
 * @brief 已有 COMPLEMENTARY 模式不受影响（回归）
 */
void test_complementary_mode_still_works(void) {
    bm_estimation_fusion_axis_t axis;

    s_sim_ax = 0.0f;
    s_sim_az = 1.0f;
    s_sim_gy = 0.0f;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode                   = BM_EST_FUSION_COMPLEMENTARY;
    axis.config.dt_s                   = 0.01f;
    axis.config.complementary.alpha    = 0.98f;
    axis.resources.read_imu            = read_imu_stub;

    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_init(&axis));
    bm_estimation_fusion_step(&axis);

    TEST_ASSERT_TRUE(isfinite(axis.state.euler.pitch_rad));
    TEST_ASSERT_TRUE(isfinite(axis.state.euler.roll_rad));
}

/* ================================================================
 * exec_ops 生命周期测试
 * ================================================================ */

/**
 * @brief exec_ops init/start/safe_stop 全路径
 */
void test_estimation_fusion_exec_ops_lifecycle(void) {
    bm_estimation_fusion_axis_t axis;
    bm_exec_t                   exec;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode            = BM_EST_FUSION_EKF_CV;
    axis.config.dt_s            = 0.01f;
    axis.config.ekf_cv.q_pos    = 0.01f;
    axis.config.ekf_cv.q_vel    = 0.01f;
    axis.config.ekf_cv.r_pos    = 0.1f;
    axis.resources.read_imu     = read_imu_stub;
    axis.resources.publish_telemetry = publish_tel_stub;

    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    /* init 应成功 */
    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_exec_ops.init(&exec));
    /* start 应返回 BM_OK */
    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_exec_ops.start(&exec));

    /* safe_stop 应清零欧拉角 */
    axis.state.euler.roll_rad  = 1.0f;
    axis.state.euler.pitch_rad = 0.5f;
    axis.state.euler.yaw_rad   = 0.3f;
    bm_estimation_fusion_exec_ops.safe_stop(&exec);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.euler.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.euler.pitch_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.euler.yaw_rad);
}

/**
 * @brief exec_ops init 对非法配置返回 BM_ERR_INVALID
 */
void test_estimation_fusion_exec_ops_init_rejects_bad_config(void) {
    bm_estimation_fusion_axis_t axis;
    bm_exec_t                   exec;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode  = BM_EST_FUSION_EKF_CV;
    axis.config.dt_s  = -1.0f; /* 非法 */

    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_estimation_fusion_exec_ops.init(&exec));
}

/**
 * @brief exec_ops run 正确转发给 step（sequence 递增）
 */
void test_estimation_fusion_exec_ops_run_forwards_to_step(void) {
    bm_estimation_fusion_axis_t axis;
    bm_exec_t                   exec;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode            = BM_EST_FUSION_EKF_CV;
    axis.config.dt_s            = 0.01f;
    axis.config.ekf_cv.q_pos    = 0.01f;
    axis.config.ekf_cv.q_vel    = 0.01f;
    axis.config.ekf_cv.r_pos    = 0.1f;
    axis.resources.read_imu     = read_imu_stub;
    axis.resources.publish_telemetry = publish_tel_stub;

    memset(&exec, 0, sizeof(exec));
    exec.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_estimation_fusion_exec_ops.init(&exec));
    bm_estimation_fusion_exec_run(&exec);

    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.step_count);
    TEST_ASSERT_EQUAL_UINT32(1u, s_tel_count);
}

/* ---------- main ---------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ekf_cv_validate_ok);
    RUN_TEST(test_ekf_cv_validate_negative_noise_fails);
    RUN_TEST(test_ekf_cv_step_runs_and_publishes);
    RUN_TEST(test_ekf_cv_pitch_converges_to_zero);
    RUN_TEST(test_ekf_cv_pitch_converges_to_30deg);
    RUN_TEST(test_complementary_mode_still_works);
    RUN_TEST(test_estimation_fusion_exec_ops_lifecycle);
    RUN_TEST(test_estimation_fusion_exec_ops_init_rejects_bad_config);
    RUN_TEST(test_estimation_fusion_exec_ops_run_forwards_to_step);
    return UNITY_END();
}

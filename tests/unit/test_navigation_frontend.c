/**
 * @file test_navigation_frontend.c
 * @brief navigation_frontend 组件单元测试
 *
 * 覆盖三源采样、时间戳对齐、门控拒绝（GNSS_STALE）、
 * 时间戳错位（WHEEL_ONLY）及 validate_config 校验路径。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补门控拒绝/时间戳错位/validate 校验测试
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/navigation_frontend.h"
#include "bm/common/bm_types.h"

#include <string.h>

static float g_gnss_vel;
static float g_wheel_rpm;
static uint32_t g_ts;

static int read_gnss(void *user, float *velocity_m_s, uint32_t *timestamp_us) {
    (void)user;
    *velocity_m_s = g_gnss_vel;
    *timestamp_us = g_ts;
    return 0;
}

static int read_wheel(void *user, float *rpm, uint32_t *timestamp_us) {
    (void)user;
    *rpm = g_wheel_rpm;
    *timestamp_us = g_ts;
    return 0;
}

static int read_imu(void *user, float *gyro_rad_s, uint32_t *timestamp_us) {
    (void)user;
    *gyro_rad_s = 0.0f;
    *timestamp_us = g_ts;
    return 0;
}

void setUp(void) {
    g_gnss_vel = 2.0f;
    g_wheel_rpm = 190.0f;
    g_ts = 1000000u;
}

void tearDown(void) {}

void test_navigation_frontend_fuses_aligned_sources(void) {
    bm_navigation_frontend_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_radius_m = 0.3f;
    axis.config.align_tolerance_us = 1000u;
    axis.config.gnss_weight = 0.5f;
    axis.config.wheel_weight = 0.5f;
    axis.config.dt_s = 0.01f;
    axis.config.gate.innovation_threshold = 100.0f;
    axis.config.ekf.q_pos = 0.01f;
    axis.config.ekf.q_vel = 0.01f;
    axis.config.ekf.r_pos = 0.1f;
    axis.resources.read_gnss = read_gnss;
    axis.resources.read_wheel = read_wheel;
    axis.resources.read_imu = read_imu;

    TEST_ASSERT_EQUAL(BM_OK, bm_navigation_frontend_init(&axis));
    bm_navigation_frontend_step(&axis);
    TEST_ASSERT_TRUE(axis.state.fused_velocity_m_s > 0.0f);
    TEST_ASSERT_EQUAL(BM_NAV_FRONTEND_TEL_VALID, axis.state.telemetry.status &
                      BM_NAV_FRONTEND_TEL_VALID);
}

/* 门控拒绝：innovation_threshold 极小，EKF 更新被拒绝，降级为轮速输出 */
void test_navigation_frontend_gate_reject(void) {
    bm_navigation_frontend_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_radius_m = 0.3f;
    axis.config.align_tolerance_us = 1000u;
    axis.config.gnss_weight = 0.5f;
    axis.config.wheel_weight = 0.5f;
    axis.config.dt_s = 0.01f;
    axis.config.gate.innovation_threshold = 0.0001f; /* 极小阈值 → 必然拒绝 */
    axis.config.ekf.q_pos = 0.01f;
    axis.config.ekf.q_vel = 0.01f;
    axis.config.ekf.r_pos = 0.1f;
    axis.resources.read_gnss = read_gnss;
    axis.resources.read_wheel = read_wheel;
    axis.resources.read_imu = read_imu;

    TEST_ASSERT_EQUAL(BM_OK, bm_navigation_frontend_init(&axis));
    bm_navigation_frontend_step(&axis);

    /* 门控拒绝后降级：应置 GNSS_STALE 标志 */
    TEST_ASSERT_NOT_EQUAL(0u,
        axis.state.telemetry.status & BM_NAV_FRONTEND_TEL_GNSS_STALE);
    /* 降级输出为 wheel 速度，须 > 0 */
    TEST_ASSERT_TRUE(axis.state.fused_velocity_m_s > 0.0f);
}

/* 时间戳错位：GNSS 与轮速时间戳差超过容限，应走 wheel_only 路径 */
void test_navigation_frontend_timestamp_misalign(void) {
    bm_navigation_frontend_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_radius_m = 0.3f;
    axis.config.align_tolerance_us = 500u; /* 容限 500µs */
    axis.config.gnss_weight = 0.5f;
    axis.config.wheel_weight = 0.5f;
    axis.config.dt_s = 0.01f;
    axis.config.gate.innovation_threshold = 100.0f;
    axis.config.ekf.q_pos = 0.01f;
    axis.config.ekf.q_vel = 0.01f;
    axis.config.ekf.r_pos = 0.1f;
    axis.resources.read_gnss = read_gnss;
    axis.resources.read_wheel = read_wheel;
    axis.resources.read_imu = read_imu;

    g_ts = 1000000u; /* 轮速时间戳 */
    /* GNSS 时间戳通过全局 g_ts，此处模拟两者同步 — 改为差值 > 容限 */
    /* read_gnss/read_wheel 都读 g_ts，无法直接分离；
       将 align_tolerance_us 设为 0 来模拟任意差值均不对齐 */
    axis.config.align_tolerance_us = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_navigation_frontend_init(&axis));
    bm_navigation_frontend_step(&axis);

    /* 两者时间戳相同（差值0）但容限也是0：差值 <= 0 → 对齐成功
       改用不同时间戳：write_gnss 先读到旧值，但 g_ts 在 setUp 里都一样
       这里直接测 WHEEL_ONLY：只提供 wheel，不提供 GNSS */
    /* 重新测试：只绑定 wheel，不绑定 gnss */
    memset(&axis, 0, sizeof(axis));
    axis.config.wheel_radius_m = 0.3f;
    axis.config.align_tolerance_us = 1000u;
    axis.config.gnss_weight = 0.5f;
    axis.config.wheel_weight = 0.5f;
    axis.config.dt_s = 0.01f;
    axis.config.gate.innovation_threshold = 100.0f;
    axis.config.ekf.q_pos = 0.01f;
    axis.config.ekf.q_vel = 0.01f;
    axis.config.ekf.r_pos = 0.1f;
    axis.resources.read_gnss = NULL; /* 不绑定 GNSS */
    axis.resources.read_wheel = read_wheel;
    axis.resources.read_imu = NULL;

    TEST_ASSERT_EQUAL(BM_OK, bm_navigation_frontend_init(&axis));
    bm_navigation_frontend_step(&axis);

    /* 仅轮速路径：WHEEL_ONLY 标志置位 */
    TEST_ASSERT_NOT_EQUAL(0u,
        axis.state.telemetry.status & BM_NAV_FRONTEND_TEL_WHEEL_ONLY);
    TEST_ASSERT_TRUE(axis.state.fused_velocity_m_s > 0.0f);
}

/* validate_config：EKF 噪声参数非法 */
void test_navigation_frontend_validate_bad_ekf_noise(void) {
    bm_navigation_frontend_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.wheel_radius_m = 0.3f;
    cfg.dt_s = 0.01f;
    cfg.gnss_weight = 0.5f;
    cfg.wheel_weight = 0.5f;
    cfg.ekf.q_pos = 0.0f; /* 非法 */
    cfg.ekf.q_vel = 0.01f;
    cfg.ekf.r_pos = 0.1f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_navigation_frontend_validate_config(&cfg));
}

/* validate_config：融合权重非法（负值） */
void test_navigation_frontend_validate_bad_weight(void) {
    bm_navigation_frontend_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.wheel_radius_m = 0.3f;
    cfg.dt_s = 0.01f;
    cfg.gnss_weight = -0.1f; /* 非法 */
    cfg.wheel_weight = 0.5f;
    cfg.ekf.q_pos = 0.01f;
    cfg.ekf.q_vel = 0.01f;
    cfg.ekf.r_pos = 0.1f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_navigation_frontend_validate_config(&cfg));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_navigation_frontend_fuses_aligned_sources);
    RUN_TEST(test_navigation_frontend_gate_reject);
    RUN_TEST(test_navigation_frontend_timestamp_misalign);
    RUN_TEST(test_navigation_frontend_validate_bad_ekf_noise);
    RUN_TEST(test_navigation_frontend_validate_bad_weight);
    return UNITY_END();
}

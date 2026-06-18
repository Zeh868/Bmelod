/**
 * @file test_navigation_frontend.c
 * @brief navigation_frontend 组件单元测试
 *
 * 覆盖三源采样、时间戳对齐与门控 EKF 速度融合基本行为。
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_navigation_frontend_fuses_aligned_sources);
    return UNITY_END();
}

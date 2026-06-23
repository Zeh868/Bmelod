/**
 * @file test_spectral_diagnostics.c
 * @brief spectral_diagnostics 组件单元测试
 *
 * 覆盖以下场景：
 *   1. Goertzel 模式：step 累积并在 block_size 帧后输出有效幅值；
 *   2. STFT 模式：合法 frame_size（64）帧填满后 step 正常执行；
 *   3. 非法 frame_size（非 FFT 支持尺寸）被 validate_config 拒绝；
 *   4. NULL 边界：接口传入 NULL 不崩溃。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-23
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-23       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/spectral_diagnostics.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/* ---------- 测试辅助 ---------- */

/** 模拟采样值 */
static float g_sample_value;
/** 控制 feed_sample 是否失败 */
static int g_sample_error;
/** 最近一次遥测副本 */
static bm_spectral_diagnostics_telemetry_t g_last_tel;
/** 遥测发布计数 */
static uint32_t g_tel_count;

/** STFT 测试用缓冲（frame_size=64） */
#define STFT_FRAME_SIZE  64u
static float g_stft_frame[STFT_FRAME_SIZE];
static float g_stft_window[STFT_FRAME_SIZE];
static float g_stft_magnitude[STFT_FRAME_SIZE / 2u + 1u];

/**
 * @brief 模拟 feed_sample 回调
 */
static int fake_feed(void *user, float *sample) {
    (void)user;
    if (g_sample_error) {
        return -1;
    }
    *sample = g_sample_value;
    return 0;
}

/**
 * @brief 模拟 publish_telemetry 回调
 */
static void fake_publish(void *user,
                         const bm_spectral_diagnostics_telemetry_t *tel) {
    (void)user;
    g_last_tel = *tel;
    g_tel_count++;
}

/* ---------- setUp / tearDown ---------- */

void setUp(void) {
    g_sample_value = 0.0f;
    g_sample_error = 0;
    memset(&g_last_tel, 0, sizeof(g_last_tel));
    g_tel_count = 0u;
    memset(g_stft_frame,     0, sizeof(g_stft_frame));
    memset(g_stft_window,    0, sizeof(g_stft_window));
    memset(g_stft_magnitude, 0, sizeof(g_stft_magnitude));
}

void tearDown(void) {}

/* ---------- 测试用例 ---------- */

/**
 * @brief Goertzel 模式：经过 block_size 拍后 status 变为 VALID，幅值 >= 0
 */
void test_spectral_goertzel_accumulates_and_outputs(void) {
    bm_spectral_diagnostics_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode = BM_SPECTRAL_MODE_GOERTZEL;
    axis.config.sample_hz = 1000.0f;
    axis.config.goertzel.target_freq_hz = 50.0f;
    axis.config.goertzel.sample_hz      = 1000.0f;
    axis.config.goertzel.block_size     = 20u;
    axis.config.pole_pairs = 1.0f;
    axis.resources.feed_sample      = fake_feed;
    axis.resources.publish_telemetry = fake_publish;

    TEST_ASSERT_EQUAL(BM_OK, bm_spectral_diagnostics_init(&axis));

    /* 喂入正弦波（50 Hz @ 1 kHz 采样） */
    for (i = 0u; i < 20u; i++) {
        g_sample_value = sinf(2.0f * 3.14159265f * 50.0f
                              * (float)i / 1000.0f);
        bm_spectral_diagnostics_step(&axis, 3000.0f /* rpm */);
    }

    /* 第 20 拍完成 Goertzel 块，status 应为 VALID */
    TEST_ASSERT_EQUAL_UINT32(BM_SPECTRAL_DIAG_TEL_VALID, g_last_tel.status);
    TEST_ASSERT_TRUE(g_last_tel.goertzel_mag >= 0.0f);
    TEST_ASSERT_EQUAL(20u, g_tel_count);
}

/**
 * @brief STFT 模式：validate_config 对合法 frame_size=64 返回 BM_OK
 */
void test_spectral_stft_valid_frame_size_accepted(void) {
    bm_spectral_diagnostics_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = BM_SPECTRAL_MODE_STFT;
    cfg.sample_hz = 1000.0f;
    cfg.stft_frame_size = STFT_FRAME_SIZE; /* 64，FFT 支持 */
    cfg.stft_frame     = g_stft_frame;
    cfg.stft_window    = g_stft_window;
    cfg.stft_magnitude = g_stft_magnitude;

    TEST_ASSERT_EQUAL(BM_OK, bm_spectral_diagnostics_validate_config(&cfg));
}

/**
 * @brief STFT 模式：非法 frame_size（非 FFT 支持尺寸）被 validate_config 拒绝
 */
void test_spectral_stft_invalid_frame_size_rejected(void) {
    bm_spectral_diagnostics_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = BM_SPECTRAL_MODE_STFT;
    cfg.sample_hz = 1000.0f;
    cfg.stft_frame_size = 100u; /* 非 FFT 支持尺寸 */
    cfg.stft_frame     = g_stft_frame;
    cfg.stft_window    = g_stft_window;
    cfg.stft_magnitude = g_stft_magnitude;

    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_spectral_diagnostics_validate_config(&cfg));
}

/**
 * @brief STFT 模式：frame_size=0 被拒绝
 */
void test_spectral_stft_zero_frame_size_rejected(void) {
    bm_spectral_diagnostics_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = BM_SPECTRAL_MODE_STFT;
    cfg.sample_hz = 1000.0f;
    cfg.stft_frame_size = 0u;
    cfg.stft_frame     = g_stft_frame;
    cfg.stft_window    = g_stft_window;
    cfg.stft_magnitude = g_stft_magnitude;

    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_spectral_diagnostics_validate_config(&cfg));
}

/**
 * @brief STFT 模式：frame 指针为 NULL 被拒绝
 */
void test_spectral_stft_null_frame_ptr_rejected(void) {
    bm_spectral_diagnostics_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = BM_SPECTRAL_MODE_STFT;
    cfg.sample_hz = 1000.0f;
    cfg.stft_frame_size = STFT_FRAME_SIZE;
    cfg.stft_frame     = NULL; /* 非法 */
    cfg.stft_window    = g_stft_window;
    cfg.stft_magnitude = g_stft_magnitude;

    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_spectral_diagnostics_validate_config(&cfg));
}

/**
 * @brief STFT 模式：合法配置 init 后 step 正常运行，不崩溃
 */
void test_spectral_stft_step_runs_without_crash(void) {
    bm_spectral_diagnostics_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.mode = BM_SPECTRAL_MODE_STFT;
    axis.config.sample_hz = 1000.0f;
    axis.config.stft_frame_size = STFT_FRAME_SIZE;
    axis.config.stft_frame     = g_stft_frame;
    axis.config.stft_window    = g_stft_window;
    axis.config.stft_magnitude = g_stft_magnitude;
    /* Goertzel 子模块仍需合法参数 */
    axis.config.goertzel.target_freq_hz = 50.0f;
    axis.config.goertzel.sample_hz      = 1000.0f;
    axis.config.goertzel.block_size     = 20u;
    axis.config.pole_pairs = 1.0f;
    axis.resources.feed_sample      = fake_feed;
    axis.resources.publish_telemetry = fake_publish;

    TEST_ASSERT_EQUAL(BM_OK, bm_spectral_diagnostics_init(&axis));

    g_sample_value = 1.0f;
    for (i = 0u; i < STFT_FRAME_SIZE + 5u; i++) {
        bm_spectral_diagnostics_step(&axis, 3000.0f);
    }

    TEST_ASSERT_TRUE(g_tel_count == STFT_FRAME_SIZE + 5u);
}

/**
 * @brief NULL 边界：接口传入 NULL 不崩溃
 */
void test_spectral_null_safety(void) {
    TEST_ASSERT_NOT_EQUAL(BM_OK,
        bm_spectral_diagnostics_validate_config(NULL));
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_spectral_diagnostics_init(NULL));
    bm_spectral_diagnostics_reset(NULL);
    bm_spectral_diagnostics_step(NULL, 0.0f);
}

/* ---------- main ---------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_spectral_goertzel_accumulates_and_outputs);
    RUN_TEST(test_spectral_stft_valid_frame_size_accepted);
    RUN_TEST(test_spectral_stft_invalid_frame_size_rejected);
    RUN_TEST(test_spectral_stft_zero_frame_size_rejected);
    RUN_TEST(test_spectral_stft_null_frame_ptr_rejected);
    RUN_TEST(test_spectral_stft_step_runs_without_crash);
    RUN_TEST(test_spectral_null_safety);
    return UNITY_END();
}

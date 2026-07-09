/**
 * @file test_radar_frontend.c
 * @brief radar_frontend 组件单元测试
 *
 * 覆盖 RFFT 距离像与杂波抑制后峰值检测。
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
#include "bm/component/radar_frontend.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_radar_frontend_detects_tone_peak(void) {
    bm_radar_frontend_axis_t axis;
    float chirp[BM_ALGO_FFT_SIZE_64];
    float profile[BM_ALGO_FFT_SIZE_64];
    float clutter[BM_ALGO_FFT_SIZE_64];
    float fft_work[BM_ALGO_FFT_SIZE_64 * 2u];
    uint32_t i;
    int pass;

    for (i = 0u; i < BM_ALGO_FFT_SIZE_64; ++i) {
        chirp[i] = sinf(2.0f * 3.14159265f * 4.0f * (float)i /
                        (float)BM_ALGO_FFT_SIZE_64);
    }

    memset(&axis, 0, sizeof(axis));
    axis.config.fft_size = BM_ALGO_FFT_SIZE_64;
    axis.config.sample_hz = 1.0e6f;
    axis.config.clutter_alpha = 0.1f;
    axis.config.range_scale_m = 0.1f;

    TEST_ASSERT_EQUAL(BM_OK, bm_radar_frontend_init(&axis, profile,
                                                  BM_ALGO_FFT_SIZE_64,
                                                  clutter, fft_work,
                                                  (uint32_t)(sizeof(fft_work) /
                                                             sizeof(fft_work[0]))));

    for (pass = 0; pass < 8; ++pass) {
        TEST_ASSERT_EQUAL(BM_OK, bm_radar_frontend_feed_chirp(&axis, chirp,
                                                              BM_ALGO_FFT_SIZE_64));
    }

    TEST_ASSERT_EQUAL_UINT32(4u, axis.state.telemetry.peak_bin);
    TEST_ASSERT_TRUE(axis.state.telemetry.peak_magnitude > 0.01f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.4f, axis.state.telemetry.peak_range_m);
}

void test_radar_frontend_reset_does_not_overrun_clutter_mean(void) {
    bm_radar_frontend_axis_t axis;
    float profile[BM_ALGO_FFT_SIZE_64];
    /* clutter_buf 前半 (fft_size/2 个) 是真正的 clutter_mean，
     * 后半是哨兵金丝雀区，reset 不应触碰。 */
    float clutter_buf[BM_ALGO_FFT_SIZE_64];
    float fft_work[BM_ALGO_FFT_SIZE_64 * 2u];
    const uint32_t bins = BM_ALGO_FFT_SIZE_64 / 2u;
    uint32_t i;

    for (i = 0u; i < BM_ALGO_FFT_SIZE_64; ++i) {
        clutter_buf[i] = (i < bins) ? 1.0f : 12345.0f;
    }

    memset(&axis, 0, sizeof(axis));
    axis.config.fft_size = BM_ALGO_FFT_SIZE_64;
    axis.config.sample_hz = 1.0e6f;
    axis.config.clutter_alpha = 0.1f;
    axis.config.range_scale_m = 0.1f;

    TEST_ASSERT_EQUAL(BM_OK, bm_radar_frontend_init(&axis, profile,
                                                  BM_ALGO_FFT_SIZE_64,
                                                  clutter_buf, fft_work,
                                                  (uint32_t)(sizeof(fft_work) /
                                                             sizeof(fft_work[0]))));

    bm_radar_frontend_reset(&axis);

    for (i = 0u; i < bins; ++i) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, clutter_buf[i]);
    }
    for (i = bins; i < BM_ALGO_FFT_SIZE_64; ++i) {
        TEST_ASSERT_EQUAL_FLOAT(12345.0f, clutter_buf[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_radar_frontend_detects_tone_peak);
    RUN_TEST(test_radar_frontend_reset_does_not_overrun_clutter_mean);
    return UNITY_END();
}

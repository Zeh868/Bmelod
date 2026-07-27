/**
 * @file test_audio_array_frontend.c
 * @brief audio_array_frontend 组件单元测试
 *
 * 覆盖固定时延 delay-and-sum 与能量输出。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            MVDR 波束模式
 * 2026-07-27       1.2            zeh            适配四段式 init：先填 resources
 * 2026-07-27       1.3            zeh            step 返回 void，去掉返回值断言
 */
#include "unity.h"
#include "bm/component/audio_array_frontend.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_audio_array_fixed_delay_beamform(void) {
    bm_audio_array_frontend_axis_t axis;
    float ch0[32];
    float ch1[32];
    float mono[32];
    float beam_buf[32];
    const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS];
    uint32_t i;

    for (i = 0u; i < 32u; ++i) {
        ch0[i] = sinf(2.0f * 3.14159265f * 100.0f * (float)i / 8000.0f);
        ch1[i] = (i >= 4u)
                     ? sinf(2.0f * 3.14159265f * 100.0f * (float)(i - 4u) / 8000.0f)
                     : 0.0f;
    }
    channels[0] = ch0;
    channels[1] = ch1;

    memset(&axis, 0, sizeof(axis));
    axis.config.num_channels = 2u;
    axis.config.block_samples = 32u;
    axis.config.sample_hz = 8000.0f;
    axis.config.use_fixed_delay = 1;
    axis.config.fixed_delay_samples[0] = 0;
    axis.config.fixed_delay_samples[1] = 4;
    axis.resources.beam_buffer = beam_buf;
    axis.resources.beam_buffer_len = 32u;
    axis.resources.gcc_work = NULL;
    axis.resources.gcc_work_count = 0u;
    axis.resources.publish_telemetry = NULL;
    axis.resources.publish_telemetry_user = NULL;

    TEST_ASSERT_EQUAL(BM_OK, bm_audio_array_frontend_init(&axis));
    bm_audio_array_frontend_step(&axis, channels, mono, 32u);
    TEST_ASSERT_TRUE(axis.state.telemetry.energy > 0.01f);
    TEST_ASSERT_EQUAL_INT32(4, axis.state.telemetry.delay_samples[1]);
}

void test_audio_array_mvdr_beamform(void) {
    bm_audio_array_frontend_axis_t axis;
    float ch0[32];
    float ch1[32];
    float mono[32];
    float beam_buf[32];
    const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS];
    uint32_t i;

    for (i = 0u; i < 32u; ++i) {
        ch0[i] = sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 8000.0f);
        ch1[i] = (i >= 4u)
                     ? sinf(2.0f * 3.14159265f * 1000.0f *
                            (float)(i - 4u) / 8000.0f)
                     : 0.0f;
    }
    channels[0] = ch0;
    channels[1] = ch1;

    memset(&axis, 0, sizeof(axis));
    axis.config.num_channels = 2u;
    axis.config.block_samples = 32u;
    axis.config.sample_hz = 8000.0f;
    axis.config.use_fixed_delay = 1;
    axis.config.fixed_delay_samples[0] = 0;
    axis.config.fixed_delay_samples[1] = 4;
    axis.config.beam_mode = BM_AUDIO_BEAM_MVDR;
    axis.config.mvdr_diagonal_load = 1e-3f;
    axis.resources.beam_buffer = beam_buf;
    axis.resources.beam_buffer_len = 32u;
    axis.resources.gcc_work = NULL;
    axis.resources.gcc_work_count = 0u;
    axis.resources.publish_telemetry = NULL;
    axis.resources.publish_telemetry_user = NULL;

    TEST_ASSERT_EQUAL(BM_OK, bm_audio_array_frontend_init(&axis));
    bm_audio_array_frontend_step(&axis, channels, mono, 32u);
    TEST_ASSERT_TRUE(axis.state.telemetry.energy > 0.01f);
}

void test_audio_array_step_null_safety(void) {
    bm_audio_array_frontend_axis_t axis;
    float ch0[32];
    float mono[32];
    const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS];

    memset(&axis, 0, sizeof(axis));
    channels[0] = ch0;

    /* NULL 实例/通道/输出均不应崩溃 */
    bm_audio_array_frontend_step(NULL, channels, mono, 32u);
    bm_audio_array_frontend_step(&axis, NULL, mono, 32u);
    bm_audio_array_frontend_step(&axis, channels, NULL, 32u);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_audio_array_fixed_delay_beamform);
    RUN_TEST(test_audio_array_mvdr_beamform);
    RUN_TEST(test_audio_array_step_null_safety);
    return UNITY_END();
}

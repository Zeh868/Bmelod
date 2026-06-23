/**
 * @file test_daq_frontend.c
 * @brief daq_frontend 组件单元测试
 *
 * 覆盖正常采集流程、预触发环形缓冲复制、未 armed 时 feed 返错及 NULL 边界。
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
#include "bm/component/daq_frontend.h"
#include "bm/common/bm_types.h"

#include <string.h>
#include <math.h>

/* ---------- 辅助宏 ---------- */
#define RMS_BUF_LEN     32u
#define PRE_BUF_LEN     8u
#define CAPTURE_SAMPLES 4u
#define TRIGGER_LEVEL   0.5f

/* ---------- 测试夹具全局缓冲区 ---------- */
static float g_rms_buf[RMS_BUF_LEN];
static float g_pre_buf[PRE_BUF_LEN];

/* ---------- 构造标准 axis 的辅助函数 ---------- */
static void make_axis(bm_daq_frontend_axis_t *axis,
                      uint32_t pre_trigger_samples,
                      uint32_t capture_samples,
                      float trigger_level) {
    memset(axis, 0, sizeof(*axis));
    axis->config.trigger_level      = trigger_level;
    axis->config.pre_trigger_samples = pre_trigger_samples;
    axis->config.capture_samples     = capture_samples;
    axis->config.sample_hz           = 1000u;
}

void setUp(void) {
    memset(g_rms_buf, 0, sizeof(g_rms_buf));
    memset(g_pre_buf, 0, sizeof(g_pre_buf));
}

void tearDown(void) {}

/* ==========================================================================
 * 测试用例
 * ========================================================================== */

/**
 * @brief arm → feed 低于触发电平 → 未触发，captured 仍为 0
 */
void test_daq_frontend_feed_below_trigger_no_capture(void) {
    bm_daq_frontend_axis_t axis;
    int ret;

    make_axis(&axis, PRE_BUF_LEN, CAPTURE_SAMPLES, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, g_pre_buf, PRE_BUF_LEN));

    bm_daq_frontend_arm(&axis);
    TEST_ASSERT_EQUAL(1, axis.state.armed);

    ret = bm_daq_frontend_feed(&axis, 0.1f);
    TEST_ASSERT_EQUAL(BM_OK, ret);
    TEST_ASSERT_EQUAL(0u, axis.state.captured);
    TEST_ASSERT_EQUAL(0, axis.state.triggered);
}

/**
 * @brief arm → feed 超过触发电平 → 触发；再 feed capture_samples 次 → CAPTURE_DONE，armed 清零
 */
void test_daq_frontend_arm_feed_trigger_capture_done(void) {
    bm_daq_frontend_axis_t axis;
    int ret;
    uint32_t i;

    make_axis(&axis, 0u, CAPTURE_SAMPLES, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, NULL, 0u));

    bm_daq_frontend_arm(&axis);

    /* 触发样本 */
    ret = bm_daq_frontend_feed(&axis, 1.0f);
    TEST_ASSERT_EQUAL(1, axis.state.triggered);
    /* 首个触发样本本身也计入 captured；若 capture_samples==1 则已完成 */
    /* 这里 capture_samples==4，第一次仅 captured==1 */
    TEST_ASSERT_EQUAL(1u, axis.state.captured);
    TEST_ASSERT_NOT_EQUAL(BM_DAQ_CAPTURE_DONE, ret); /* 还未满 4 次 */

    /* 再 feed 3 次，最后一次返回 CAPTURE_DONE */
    for (i = 0u; i < CAPTURE_SAMPLES - 2u; i++) {
        ret = bm_daq_frontend_feed(&axis, 1.0f);
        TEST_ASSERT_NOT_EQUAL(BM_DAQ_CAPTURE_DONE, ret);
    }
    ret = bm_daq_frontend_feed(&axis, 1.0f);
    TEST_ASSERT_EQUAL(BM_DAQ_CAPTURE_DONE, ret);
    TEST_ASSERT_EQUAL(0, axis.state.armed);
    TEST_ASSERT_EQUAL(CAPTURE_SAMPLES, axis.state.captured);
}

/**
 * @brief 预触发缓冲：arm → 推入 PRE_BUF_LEN 个小样本 → 触发 → copy_pre_trigger 按时序正确
 */
void test_daq_frontend_pre_trigger_copy_correct_order(void) {
    bm_daq_frontend_axis_t axis;
    float dst[PRE_BUF_LEN];
    uint32_t n;
    uint32_t i;

    make_axis(&axis, PRE_BUF_LEN, 16u, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, g_pre_buf, PRE_BUF_LEN));

    bm_daq_frontend_arm(&axis);

    /* 推入 PRE_BUF_LEN 个已知样本（值 = 索引 × 0.01f），均低于触发电平 */
    for (i = 0u; i < PRE_BUF_LEN; i++) {
        bm_daq_frontend_feed(&axis, (float)i * 0.01f);
    }
    TEST_ASSERT_EQUAL(0, axis.state.triggered);
    TEST_ASSERT_EQUAL(PRE_BUF_LEN, axis.state.pre_trigger_count);

    /* 触发 */
    bm_daq_frontend_feed(&axis, 1.0f);
    TEST_ASSERT_EQUAL(1, axis.state.triggered);

    /* 复制预触发数据：最早的样本排在 dst[0] */
    n = bm_daq_frontend_copy_pre_trigger(&axis, dst, PRE_BUF_LEN);
    TEST_ASSERT_EQUAL(PRE_BUF_LEN, n);

    /* 缓冲已满（wrap-around）：最旧样本为 index=0 的 0.0f，最新为 (PRE_BUF_LEN-1)*0.01f */
    for (i = 0u; i < PRE_BUF_LEN; i++) {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, (float)i * 0.01f, dst[i]);
    }
}

/**
 * @brief 预触发缓冲未满时 copy，只返回实际样本数，内容与写入顺序一致
 */
void test_daq_frontend_pre_trigger_partial_copy(void) {
    bm_daq_frontend_axis_t axis;
    float dst[PRE_BUF_LEN];
    uint32_t n;

    make_axis(&axis, PRE_BUF_LEN, 16u, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, g_pre_buf, PRE_BUF_LEN));

    bm_daq_frontend_arm(&axis);

    /* 仅推入 3 个样本 */
    bm_daq_frontend_feed(&axis, 0.1f);
    bm_daq_frontend_feed(&axis, 0.2f);
    bm_daq_frontend_feed(&axis, 0.3f);

    n = bm_daq_frontend_copy_pre_trigger(&axis, dst, PRE_BUF_LEN);
    TEST_ASSERT_EQUAL(3u, n);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.1f, dst[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.2f, dst[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.3f, dst[2]);
}

/**
 * @brief 未 armed 时 feed 返回 BM_ERR_INVALID
 */
void test_daq_frontend_feed_without_arm_returns_invalid(void) {
    bm_daq_frontend_axis_t axis;
    int ret;

    make_axis(&axis, 0u, CAPTURE_SAMPLES, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, NULL, 0u));

    /* 不调用 arm，直接 feed */
    ret = bm_daq_frontend_feed(&axis, 1.0f);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, ret);
    TEST_ASSERT_EQUAL(0u, axis.state.captured);
}

/**
 * @brief reset 之后 armed/triggered/captured/peak 全部清零，再次 arm 正常工作
 */
void test_daq_frontend_reset_clears_state(void) {
    bm_daq_frontend_axis_t axis;

    make_axis(&axis, 0u, CAPTURE_SAMPLES, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, NULL, 0u));

    bm_daq_frontend_arm(&axis);
    bm_daq_frontend_feed(&axis, 1.0f); /* 触发 */

    bm_daq_frontend_reset(&axis);
    TEST_ASSERT_EQUAL(0, axis.state.armed);
    TEST_ASSERT_EQUAL(0, axis.state.triggered);
    TEST_ASSERT_EQUAL(0u, axis.state.captured);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, axis.state.peak);
}

/**
 * @brief peak 跟踪绝对值最大样本
 */
void test_daq_frontend_peak_tracks_max_abs(void) {
    bm_daq_frontend_axis_t axis;

    make_axis(&axis, 0u, 32u, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_OK, bm_daq_frontend_init(
        &axis, g_rms_buf, RMS_BUF_LEN, NULL, 0u));

    bm_daq_frontend_arm(&axis);
    bm_daq_frontend_feed(&axis, 0.3f);
    bm_daq_frontend_feed(&axis, -0.8f); /* 绝对值最大，尚未触发 */
    bm_daq_frontend_feed(&axis, 0.6f);  /* 触发 */

    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.8f, axis.state.peak);
}

/* ---------- NULL 边界测试 ---------- */

/**
 * @brief init 传 NULL axis 返回 BM_ERR_INVALID
 */
void test_daq_frontend_init_null_axis(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_daq_frontend_init(NULL, g_rms_buf, RMS_BUF_LEN, NULL, 0u));
}

/**
 * @brief init 传 NULL rms_buffer 返回 BM_ERR_INVALID
 */
void test_daq_frontend_init_null_rms_buffer(void) {
    bm_daq_frontend_axis_t axis;
    make_axis(&axis, 0u, CAPTURE_SAMPLES, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_daq_frontend_init(&axis, NULL, RMS_BUF_LEN, NULL, 0u));
}

/**
 * @brief feed 传 NULL axis 返回 BM_ERR_INVALID
 */
void test_daq_frontend_feed_null_axis(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_daq_frontend_feed(NULL, 1.0f));
}

/**
 * @brief copy_pre_trigger NULL 参数均返回 0
 */
void test_daq_frontend_copy_null_params(void) {
    bm_daq_frontend_axis_t axis;
    float dst[4];

    make_axis(&axis, 0u, CAPTURE_SAMPLES, TRIGGER_LEVEL);
    TEST_ASSERT_EQUAL(0u, bm_daq_frontend_copy_pre_trigger(NULL, dst, 4u));
    TEST_ASSERT_EQUAL(0u, bm_daq_frontend_copy_pre_trigger(&axis, NULL, 4u));
    TEST_ASSERT_EQUAL(0u, bm_daq_frontend_copy_pre_trigger(&axis, dst, 0u));
}

/**
 * @brief arm/reset 传 NULL 不崩溃
 */
void test_daq_frontend_arm_reset_null_safe(void) {
    bm_daq_frontend_arm(NULL);
    bm_daq_frontend_reset(NULL);
    /* 若能到达这里即通过 */
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_daq_frontend_feed_below_trigger_no_capture);
    RUN_TEST(test_daq_frontend_arm_feed_trigger_capture_done);
    RUN_TEST(test_daq_frontend_pre_trigger_copy_correct_order);
    RUN_TEST(test_daq_frontend_pre_trigger_partial_copy);
    RUN_TEST(test_daq_frontend_feed_without_arm_returns_invalid);
    RUN_TEST(test_daq_frontend_reset_clears_state);
    RUN_TEST(test_daq_frontend_peak_tracks_max_abs);
    RUN_TEST(test_daq_frontend_init_null_axis);
    RUN_TEST(test_daq_frontend_init_null_rms_buffer);
    RUN_TEST(test_daq_frontend_feed_null_axis);
    RUN_TEST(test_daq_frontend_copy_null_params);
    RUN_TEST(test_daq_frontend_arm_reset_null_safe);
    return UNITY_END();
}

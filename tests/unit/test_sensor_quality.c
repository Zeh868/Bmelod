/**
 * @file test_sensor_quality.c
 * @brief sensor_quality 组件单元测试
 *
 * 覆盖以下场景：
 *   1. 范围故障（over-range / under-range）触发验证；
 *   2. 冻结值检测（frozen）：连续 N 拍差值不变则触发；
 *   3. 正常路径：合法采样不产生故障；
 *   4. NULL 边界：axis 为 NULL 时各接口静默返回不崩溃。
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
#include "bm/component/sensor_quality.h"
#include "bm/algorithm/bm_algo_signal_quality.h"
#include "bm/common/bm_types.h"

#include <string.h>

/* ---------- 测试辅助 ---------- */

/** 模拟采样值（由测试用例写入） */
static float g_sample_value;
/** 控制 read_sample 是否返回错误（非零 = 返回失败） */
static int g_sample_error;
/** 最近一次遥测副本 */
static bm_sensor_quality_telemetry_t g_last_tel;
/** 遥测发布计数 */
static uint32_t g_tel_count;

/**
 * @brief 模拟 read_sample 回调：返回 g_sample_value；g_sample_error 非零时失败
 */
static int fake_read_sample(void *user, float *sample) {
    (void)user;
    if (g_sample_error) {
        return -1;
    }
    *sample = g_sample_value;
    return 0;
}

/**
 * @brief 模拟 publish_telemetry 回调：记录遥测并计数
 */
static void fake_publish(void *user, const bm_sensor_quality_telemetry_t *tel) {
    (void)user;
    g_last_tel = *tel;
    g_tel_count++;
}

/**
 * @brief 构造一个基础配置轴（range [-10, 10]，无变化率限制，冻结阈 3 拍）
 *
 * @param axis 待初始化的轴实例（调用方提供）
 */
static void make_axis(bm_sensor_quality_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.monitor.min_v = -10.0f;
    axis->config.monitor.max_v =  10.0f;
    axis->config.monitor.max_rate_per_s = 1000.0f; /* 不限速 */
    axis->config.frozen_epsilon = 0.001f;
    axis->config.frozen_count_required = 3u;
    axis->config.dt_s = 0.01f;
    axis->resources.read_sample = fake_read_sample;
    axis->resources.publish_telemetry = fake_publish;
}

/* ---------- setUp / tearDown ---------- */

void setUp(void) {
    g_sample_value = 0.0f;
    g_sample_error = 0;
    memset(&g_last_tel, 0, sizeof(g_last_tel));
    g_tel_count = 0u;
}

void tearDown(void) {}

/* ---------- 测试用例 ---------- */

/**
 * @brief 采样值超出 max_v 时 fault_flags 含 BM_ALGO_FAULT_OVER_RANGE
 */
void test_sensor_quality_range_fault_over(void) {
    bm_sensor_quality_axis_t axis;

    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_init(&axis, 0.0f));

    g_sample_value = 15.0f; /* > max_v=10 */
    bm_sensor_quality_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u,
        axis.state.fault_flags & BM_ALGO_FAULT_OVER_RANGE);
    TEST_ASSERT_EQUAL_UINT32(BM_SENSOR_QUALITY_TEL_VALID,
        g_last_tel.status);
    TEST_ASSERT_NOT_EQUAL(0u,
        g_last_tel.fault_flags & BM_ALGO_FAULT_OVER_RANGE);
}

/**
 * @brief 采样值低于 min_v 时 fault_flags 含 BM_ALGO_FAULT_UNDER_RANGE
 */
void test_sensor_quality_range_fault_under(void) {
    bm_sensor_quality_axis_t axis;

    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_init(&axis, 0.0f));

    g_sample_value = -15.0f; /* < min_v=-10 */
    bm_sensor_quality_step(&axis);

    TEST_ASSERT_NOT_EQUAL(0u,
        axis.state.fault_flags & BM_ALGO_FAULT_UNDER_RANGE);
    TEST_ASSERT_NOT_EQUAL(0u,
        g_last_tel.fault_flags & BM_ALGO_FAULT_UNDER_RANGE);
}

/**
 * @brief 连续 frozen_count_required+1 拍相同值后触发 BM_ALGO_FAULT_FROZEN
 *
 * 注意：第一拍 step_count==0 时不触发，step_count>0 且计数达标后触发。
 */
void test_sensor_quality_frozen_detection(void) {
    bm_sensor_quality_axis_t axis;
    uint32_t i;

    make_axis(&axis);
    axis.config.frozen_count_required = 3u;
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_init(&axis, 0.0f));

    g_sample_value = 5.0f; /* 合法，但保持不变 */
    /* 推进足够步数让冻结计数器达到阈值 */
    for (i = 0u; i < 5u; i++) {
        bm_sensor_quality_step(&axis);
    }

    TEST_ASSERT_NOT_EQUAL(0u,
        axis.state.fault_flags & BM_ALGO_FAULT_FROZEN);
    TEST_ASSERT_EQUAL(5u, g_tel_count);
}

/**
 * @brief 冻结后改变采样值，frozen_count 复位，故障消除
 */
void test_sensor_quality_frozen_clears_on_change(void) {
    bm_sensor_quality_axis_t axis;
    uint32_t i;

    make_axis(&axis);
    axis.config.frozen_count_required = 3u;
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_init(&axis, 0.0f));

    g_sample_value = 5.0f;
    for (i = 0u; i < 5u; i++) {
        bm_sensor_quality_step(&axis);
    }
    /* 此时应已触发冻结 */
    TEST_ASSERT_NOT_EQUAL(0u,
        axis.state.fault_flags & BM_ALGO_FAULT_FROZEN);

    /* 改变采样值，冻结应消除 */
    g_sample_value = 6.0f;
    bm_sensor_quality_step(&axis);

    TEST_ASSERT_EQUAL(0u,
        axis.state.fault_flags & BM_ALGO_FAULT_FROZEN);
}

/**
 * @brief 合法采样路径：fault_flags 为 0，遥测 status 为 VALID
 */
void test_sensor_quality_normal_path_no_fault(void) {
    bm_sensor_quality_axis_t axis;
    uint32_t i;

    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_init(&axis, 0.0f));

    /* 在合法范围内变化采样，不会冻结也不会越限 */
    for (i = 0u; i < 10u; i++) {
        g_sample_value = (float)i * 0.5f; /* 0..4.5，均在 [-10,10] 内 */
        bm_sensor_quality_step(&axis);
    }

    TEST_ASSERT_EQUAL(0u,
        axis.state.fault_flags & (BM_ALGO_FAULT_OVER_RANGE |
                                   BM_ALGO_FAULT_UNDER_RANGE |
                                   BM_ALGO_FAULT_FROZEN));
    TEST_ASSERT_EQUAL_UINT32(BM_SENSOR_QUALITY_TEL_VALID, g_last_tel.status);
    TEST_ASSERT_EQUAL(10u, g_tel_count);
}

/**
 * @brief read_sample 失败时遥测 status 为 STALE，last_value 保持不变
 */
void test_sensor_quality_stale_on_read_fail(void) {
    bm_sensor_quality_axis_t axis;

    make_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_init(&axis, 3.0f));

    /* 先正常步进一次建立 last_value */
    g_sample_value = 3.0f;
    bm_sensor_quality_step(&axis);
    TEST_ASSERT_EQUAL_UINT32(BM_SENSOR_QUALITY_TEL_VALID, g_last_tel.status);

    /* 下一拍 read_sample 失败 */
    g_sample_error = 1;
    bm_sensor_quality_step(&axis);

    TEST_ASSERT_EQUAL_UINT32(BM_SENSOR_QUALITY_TEL_STALE, g_last_tel.status);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, g_last_tel.value);
}

/**
 * @brief NULL 边界：各接口传入 NULL 时不崩溃
 */
void test_sensor_quality_null_safety(void) {
    /* validate_config(NULL) 必须返回非零错误 */
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_sensor_quality_validate_config(NULL));

    /* 其余接口静默返回 */
    bm_sensor_quality_reset(NULL, 0.0f);
    bm_sensor_quality_step(NULL);

    /* exec_ops NULL 安全 */
    bm_sensor_quality_exec_run(NULL);
    TEST_ASSERT_NOT_EQUAL(BM_OK, bm_sensor_quality_exec_init(NULL));
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_exec_start(NULL));
    bm_sensor_quality_exec_safe_stop(NULL);
}

/**
 * @brief exec_ops 生命周期：init → start → run → safe_stop
 */
void test_sensor_quality_exec_ops_lifecycle(void) {
    bm_sensor_quality_axis_t axis;
    bm_exec_t inst;

    make_axis(&axis);
    memset(&inst, 0, sizeof(inst));
    inst.state = &axis;

    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_exec_init(&inst));
    TEST_ASSERT_EQUAL(BM_OK, bm_sensor_quality_exec_start(&inst));

    g_sample_value = 1.0f;
    bm_sensor_quality_exec_run(&inst);
    TEST_ASSERT_EQUAL(1u, g_tel_count);

    bm_sensor_quality_exec_safe_stop(&inst);
    TEST_ASSERT_EQUAL(0u, axis.state.fault_flags);
}

/* ---------- main ---------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_quality_range_fault_over);
    RUN_TEST(test_sensor_quality_range_fault_under);
    RUN_TEST(test_sensor_quality_frozen_detection);
    RUN_TEST(test_sensor_quality_frozen_clears_on_change);
    RUN_TEST(test_sensor_quality_normal_path_no_fault);
    RUN_TEST(test_sensor_quality_stale_on_read_fail);
    RUN_TEST(test_sensor_quality_null_safety);
    RUN_TEST(test_sensor_quality_exec_ops_lifecycle);
    return UNITY_END();
}

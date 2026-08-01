/**
 * @file test_process_sequence.c
 * @brief process_sequence 组件单元测试
 *
 * 覆盖 TON/TOF 定时器、顺序步进、联锁资源与 exec 生命周期适配。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-08-01       1.1            zeh          补兼容布局与 exec 生命周期测试
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/process_sequence.h"
#include "bm/common/bm_types.h"

#include <stddef.h>
#include <string.h>

static int g_interlock_step;
static int g_interlock_allow;

/**
 * @brief 既有 step API 使用的测试联锁
 * @param user 未使用的用户上下文
 * @param step_index 当前步索引
 * @return 1 允许推进；0 阻塞推进
 */
static int interlock_ok(void *user, uint32_t step_index) {
    (void)user;
    if (!g_interlock_allow) {
        return 0;
    }
    return (step_index == g_interlock_step) ? 1 : 0;
}

/** @brief exec 资源回调探针 */
typedef struct {
    int allow;          /**< 是否允许推进 */
    uint32_t call_count;/**< 回调次数 */
    uint32_t last_step; /**< 最近步索引 */
} exec_interlock_probe_t;

/**
 * @brief 验证 exec 适配层传递 callback/user 资源
 * @param user exec_interlock_probe_t 探针
 * @param step_index 当前步索引
 * @return 探针配置的允许状态
 */
static int exec_interlock(void *user, uint32_t step_index) {
    exec_interlock_probe_t *probe = (exec_interlock_probe_t *)user;

    if (probe == NULL) {
        return 0;
    }
    probe->call_count++;
    probe->last_step = step_index;
    return probe->allow;
}

void setUp(void) {
    g_interlock_step = 0u;
    g_interlock_allow = 0;
}

void tearDown(void) {}

void test_process_ton_delays_output(void) {
    bm_process_ton_state_t ton;

    bm_process_ton_reset(&ton, 3u);
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(&ton, 1));
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(&ton, 1));
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(&ton, 1));
    TEST_ASSERT_EQUAL(1, bm_process_ton_step(&ton, 1));
}

void test_process_tof_holds_output(void) {
    bm_process_tof_state_t tof;

    bm_process_tof_reset(&tof, 2u);
    TEST_ASSERT_EQUAL(1, bm_process_tof_step(&tof, 1));
    TEST_ASSERT_EQUAL(1, bm_process_tof_step(&tof, 0));
    TEST_ASSERT_EQUAL(1, bm_process_tof_step(&tof, 0));
    TEST_ASSERT_EQUAL(0, bm_process_tof_step(&tof, 0));
}

void test_process_sequence_advances_on_timeout(void) {
    bm_process_sequence_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.step_count = 2u;
    axis.config.dt_s = 0.1f;
    axis.config.steps[0].timeout_s = 0.2f;
    axis.config.steps[1].timeout_s = 0.1f;
    TEST_ASSERT_EQUAL(BM_OK, bm_process_sequence_validate_config(&axis.config));
    bm_process_sequence_reset(&axis);
    bm_process_sequence_start(&axis);

    for (i = 0u; i < 10u; ++i) {
        bm_process_sequence_step(&axis, NULL, NULL);
    }

    TEST_ASSERT_TRUE(axis.state.done != 0);
    TEST_ASSERT_EQUAL_UINT32(2u, axis.state.current_step);
}

void test_process_sequence_waits_for_interlock(void) {
    bm_process_sequence_axis_t axis;
    uint32_t i;

    memset(&axis, 0, sizeof(axis));
    axis.config.step_count = 1u;
    axis.config.dt_s = 0.1f;
    axis.config.steps[0].timeout_s = 0.05f;
    g_interlock_step = 0u;
    bm_process_sequence_reset(&axis);
    bm_process_sequence_start(&axis);

    for (i = 0u; i < 3u; ++i) {
        bm_process_sequence_step(&axis, interlock_ok, NULL);
    }
    TEST_ASSERT_EQUAL(0, axis.state.done);

    g_interlock_allow = 1;
    g_interlock_step = 0u;
    bm_process_sequence_step(&axis, interlock_ok, NULL);
    TEST_ASSERT_TRUE(axis.state.done != 0);
}

void test_process_sequence_axis_layout_and_initializer_compatible(void) {
    typedef struct {
        bm_process_sequence_config_t config;
        bm_process_sequence_state_t state;
    } legacy_axis_layout_t;
    bm_process_sequence_axis_t axis = {
        .config = {
            .step_count = 1u,
            .steps = {{.timeout_s = 0.25f}},
            .dt_s = 0.05f
        },
        .state = {
            .current_step = 0u,
            .running = 1
        }
    };

    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(legacy_axis_layout_t),
                             (uint32_t)sizeof(bm_process_sequence_axis_t));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)offsetof(legacy_axis_layout_t, state),
                             (uint32_t)offsetof(bm_process_sequence_axis_t, state));
    TEST_ASSERT_EQUAL_UINT32(1u, axis.config.step_count);
    TEST_ASSERT_EQUAL(1, axis.state.running);
}

void test_process_sequence_null_boundaries(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_validate_config(NULL));
    bm_process_ton_reset(NULL, 1u);
    TEST_ASSERT_EQUAL(0, bm_process_ton_step(NULL, 1));
    bm_process_tof_reset(NULL, 1u);
    TEST_ASSERT_EQUAL(0, bm_process_tof_step(NULL, 1));
    bm_process_sequence_reset(NULL);
    bm_process_sequence_start(NULL);
    bm_process_sequence_step(NULL, NULL, NULL);
}

void test_process_sequence_exec_lifecycle_and_resources(void) {
    bm_process_sequence_axis_t axis;
    exec_interlock_probe_t probe;
    bm_process_sequence_exec_context_t context;
    bm_exec_t exec;

    memset(&axis, 0, sizeof(axis));
    axis.config.step_count = 1u;
    axis.config.dt_s = 0.1f;
    axis.config.steps[0].timeout_s = 0.1f;
    axis.state.current_step = 7u;
    axis.state.running = 1;

    memset(&probe, 0, sizeof(probe));
    context.axis = &axis;
    context.resources.interlock = exec_interlock;
    context.resources.interlock_user = &probe;

    memset(&exec, 0, sizeof(exec));
    exec.state = &context;
    exec.ops = &bm_process_sequence_exec_ops;

    TEST_ASSERT_EQUAL(BM_OK, exec.ops->init(&exec));
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.current_step);
    TEST_ASSERT_EQUAL(0, axis.state.running);
    TEST_ASSERT_EQUAL(BM_OK, exec.ops->start(&exec));
    TEST_ASSERT_EQUAL(1, axis.state.running);

    bm_process_sequence_exec_run(&exec);
    TEST_ASSERT_EQUAL_UINT32(1u, probe.call_count);
    TEST_ASSERT_EQUAL_UINT32(0u, probe.last_step);
    TEST_ASSERT_EQUAL(0, axis.state.done);

    probe.allow = 1;
    bm_process_sequence_exec_run(&exec);
    TEST_ASSERT_EQUAL_UINT32(2u, probe.call_count);
    TEST_ASSERT_EQUAL(1, axis.state.done);

    exec.ops->safe_stop(&exec);
    TEST_ASSERT_EQUAL_UINT32(0u, axis.state.current_step);
    TEST_ASSERT_EQUAL(0, axis.state.running);
    TEST_ASSERT_EQUAL(0, axis.state.done);
}

void test_process_sequence_exec_rejects_invalid_context(void) {
    bm_process_sequence_axis_t axis;
    bm_process_sequence_exec_context_t context;
    bm_exec_t exec;

    memset(&axis, 0, sizeof(axis));
    memset(&context, 0, sizeof(context));
    memset(&exec, 0, sizeof(exec));

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_exec_init(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_exec_start(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_exec_init(&exec));

    exec.state = &context;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_exec_init(&exec));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_exec_start(&exec));

    context.axis = &axis;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_process_sequence_exec_init(&exec));

    bm_process_sequence_exec_run(NULL);
    bm_process_sequence_exec_safe_stop(NULL);
    exec.state = NULL;
    bm_process_sequence_exec_run(&exec);
    bm_process_sequence_exec_safe_stop(&exec);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_process_ton_delays_output);
    RUN_TEST(test_process_tof_holds_output);
    RUN_TEST(test_process_sequence_advances_on_timeout);
    RUN_TEST(test_process_sequence_waits_for_interlock);
    RUN_TEST(test_process_sequence_axis_layout_and_initializer_compatible);
    RUN_TEST(test_process_sequence_null_boundaries);
    RUN_TEST(test_process_sequence_exec_lifecycle_and_resources);
    RUN_TEST(test_process_sequence_exec_rejects_invalid_context);
    return UNITY_END();
}

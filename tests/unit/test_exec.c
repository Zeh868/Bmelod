/**
 * @file test_exec.c
 * @brief 控制器实例生命周期、HRT 槽绑定与失败回滚单元测试
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#include "unity.h"
#include "bm_exec.h"
#include "bm_hrt.h"
#include "bm/core/bm_cpu_local.h"
#include "bm/hybrid/bm_timestamp.h"
#include "bm_hal_adc_sim.h"
#include "bm_hal_pwm_sim.h"
#include "bm_hal_timer_native.h"
#include "bm_log.h"
#include "hal/bm_hal_timer.h"

#include <string.h>

/* 各 mock 回调的调用计数 */
static uint32_t g_step_count;
static uint32_t g_hw_step_count;
static uint32_t g_safe_stop_count;
static uint32_t g_inst2_started;
static uint32_t g_inst2_premature_steps;
static uint32_t g_fire_hardware_during_stop;
static uint32_t g_wrong_safe_stop_count;
static uint32_t g_block_run_count;

typedef struct {
    uint32_t samples[4];
} exec_test_payload_t;

static exec_test_payload_t g_stream_payloads[2];
static bm_block_t g_stream_blocks[2];
static bm_stream_t g_stream;

static int mock_init(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static int mock_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void mock_safe_stop(const bm_exec_t *instance) {
    (void)instance;
    g_safe_stop_count++;
    if (g_fire_hardware_during_stop) {
        bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
    }
}

static void wrong_safe_stop(const bm_exec_t *instance) {
    (void)instance;
    g_wrong_safe_stop_count++;
}

static int init_fail_id2(const bm_exec_t *instance) {
    if (instance->id == 2u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

static int start_fail_id2(const bm_exec_t *instance) {
    if (instance->id == 2u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

static int bind_fail(const bm_exec_t *instance,
                    const bm_hal_hrt_binding_t *binding) {
    (void)instance;
    (void)binding;
    return BM_ERR_INVALID;
}

static void scheduled_step(const bm_exec_t *instance) {
    (void)instance;
    g_step_count++;
}

static void inst2_scheduled_step(const bm_exec_t *instance) {
    if (instance->id == 2u && g_inst2_started == 0u) {
        g_inst2_premature_steps++;
    }
    g_step_count++;
}

static int inst1_start_advance_ticks(const bm_exec_t *instance) {
    if (instance->id == 1u) {
        bm_hal_timer_native_advance_ticks(15u);
    }
    return BM_OK;
}

static int inst2_start_mark(const bm_exec_t *instance) {
    if (instance->id == 2u) {
        g_inst2_started = 1u;
    }
    return BM_OK;
}

static void hardware_step(const bm_exec_t *instance) {
    (void)instance;
    g_hw_step_count++;
}

static int stream_init(const bm_exec_t *instance) {
    (void)instance;
    memset(&g_stream, 0, sizeof(g_stream));
    g_stream.blocks = g_stream_blocks;
    g_stream.block_count = 2u;
    g_stream.block_capacity = 2u;
    g_stream.policy = BM_STREAM_POLICY_DROP_NEWEST;
    return bm_stream_init(&g_stream, g_stream_payloads, 2u,
                          sizeof(exec_test_payload_t));
}

static int stream_init_wrong_owner(const bm_exec_t *instance) {
    (void)instance;
    memset(&g_stream, 0, sizeof(g_stream));
    g_stream.blocks = g_stream_blocks;
    g_stream.block_count = 2u;
    g_stream.block_capacity = 2u;
    g_stream.policy = BM_STREAM_POLICY_DROP_NEWEST;
    g_stream.owner_cpu = 0u;
    return bm_stream_init(&g_stream, g_stream_payloads, 2u,
                          sizeof(exec_test_payload_t));
}

static int stream_start_with_ready_block(const bm_exec_t *instance) {
    bm_block_t *block;

    (void)instance;
    if (bm_stream_producer_acquire(&g_stream, &block) != BM_OK) {
        return BM_ERR_INVALID;
    }
    return bm_stream_producer_commit(&g_stream, block,
                                     sizeof(exec_test_payload_t), NULL);
}

static int stream_start_with_late_block(const bm_exec_t *instance) {
    bm_block_t *block;
    bm_timestamp_t ts;

    (void)instance;
    if (bm_stream_producer_acquire(&g_stream, &block) != BM_OK) {
        return BM_ERR_INVALID;
    }
    ts.clock_id = BM_TIMESTAMP_CLOCK_HRT;
    ts.quality = 1u;
    ts.clock_epoch = bm_hal_timer_clock_epoch_for_cpu(0u);
    ts.ticks = bm_hal_timer_get_ticks() - 200u;
    ts.rate_hz = bm_hal_timer_get_freq();
    return bm_stream_producer_commit(&g_stream, block,
                                     sizeof(exec_test_payload_t), &ts);
}

static int stream_start_with_foreign_clock_block(const bm_exec_t *instance) {
    bm_block_t *block;
    bm_timestamp_t ts;

    (void)instance;
    if (bm_stream_producer_acquire(&g_stream, &block) != BM_OK) {
        return BM_ERR_INVALID;
    }
    ts.clock_id = 1u;
    ts.quality = 1u;
    ts.clock_epoch = 0u;
    ts.ticks = bm_hal_timer_get_ticks() - 200u;
    ts.rate_hz = bm_hal_timer_get_freq();
    return bm_stream_producer_commit(&g_stream, block,
                                     sizeof(exec_test_payload_t), &ts);
}

static int stream_start_with_wrapped_late_block(const bm_exec_t *instance) {
    bm_block_t *block;
    bm_timestamp_t ts;

    (void)instance;
    if (bm_stream_producer_acquire(&g_stream, &block) != BM_OK) {
        return BM_ERR_INVALID;
    }
    ts.clock_id = BM_TIMESTAMP_CLOCK_HRT;
    ts.quality = 1u;
    ts.clock_epoch = bm_hal_timer_clock_epoch_for_cpu(0u);
    ts.ticks = 0xFFFFFFF0u;
    ts.rate_hz = bm_hal_timer_get_freq();
    return bm_stream_producer_commit(&g_stream, block,
                                     sizeof(exec_test_payload_t), &ts);
}

static void stream_block_step(const bm_exec_t *instance, bm_block_t *block) {
    (void)instance;
    (void)block;
    g_block_run_count++;
}

static void mock_ready_handler(bm_stream_t *stream, bm_block_t *block,
                               void *context) {
    (void)stream;
    (void)block;
    (void)context;
}

/*
 * 模拟单核 legacy 误配：stream 既挂 on_ready 回调，又被绑定到 exec block 槽
 *（由 exec_drain_streams 消费）。直接置位 on_ready 以贴近"绕过 setter 守卫"
 * 的真实误用路径，验证 exec 验证期拒绝双消费者组合（契约 3）。
 */
static int stream_init_with_on_ready(const bm_exec_t *instance) {
    int rc = stream_init(instance);

    if (rc != BM_OK) {
        return rc;
    }
    g_stream.on_ready = mock_ready_handler;
    return BM_OK;
}

static int bind_adc(const bm_exec_t *instance,
                    const bm_hal_hrt_binding_t *binding) {
    static const bm_hal_adc_t *adc;
    (void)instance;
    adc = &BM_HAL_ADC_SIM0;
    return bm_hal_adc_bind_complete(adc, binding);
}

/* mock 控制器 ops：init/start/safe_stop */
static const bm_exec_ops_t mock_ops = {
    mock_init,
    mock_start,
    mock_safe_stop
};

static const bm_exec_ops_t inst1_advance_ops = {
    mock_init,
    inst1_start_advance_ticks,
    mock_safe_stop
};

static const bm_exec_ops_t inst2_mark_ops = {
    mock_init,
    inst2_start_mark,
    mock_safe_stop
};

static const bm_exec_slot_t scheduled_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = 1000u,
        .run = scheduled_step,
        .name = "sched"
    },
};

static const bm_exec_slot_t hardware_slots[] = {
    {
        .kind = BM_EXEC_SLOT_HARDWARE,
        .run = hardware_step,
        .bind = bind_adc,
        .name = "hw"
    },
};

/* 定时调度槽实例（1000us 周期） */
static const bm_exec_t sched_inst = {
    1u, 0u, "sched", NULL, NULL, NULL,
    scheduled_slots,
    (uint32_t)(sizeof(scheduled_slots) / sizeof(scheduled_slots[0])),
    NULL, 0u, &mock_ops
};

static const bm_exec_slot_t inst2_scheduled_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = 1000u,
        .run = inst2_scheduled_step,
        .name = "sched2"
    },
};

static const bm_exec_t sched_inst_advance = {
    1u, 0u, "sched_adv", NULL, NULL, NULL,
    scheduled_slots,
    (uint32_t)(sizeof(scheduled_slots) / sizeof(scheduled_slots[0])),
    NULL, 0u, &inst1_advance_ops
};

static const bm_exec_t sched_inst2 = {
    2u, 0u, "sched2", NULL, NULL, NULL,
    inst2_scheduled_slots,
    (uint32_t)(sizeof(inst2_scheduled_slots) / sizeof(inst2_scheduled_slots[0])),
    NULL, 0u, &inst2_mark_ops
};

/* 硬件触发槽实例（ADC 完成中断） */
static const bm_exec_t hw_inst = {
    2u, 0u, "hw", NULL, NULL, NULL,
    hardware_slots,
    (uint32_t)(sizeof(hardware_slots) / sizeof(hardware_slots[0])),
    NULL, 0u, &mock_ops
};

static const bm_exec_ops_t fail_init_ops = {
    init_fail_id2, mock_start, mock_safe_stop
};

static const bm_exec_ops_t fail_start_ops = {
    mock_init, start_fail_id2, mock_safe_stop
};

static const bm_exec_ops_t fail_bind_ops = {
    mock_init, mock_start, mock_safe_stop
};

static const bm_exec_ops_t wrong_ops = {
    mock_init, mock_start, wrong_safe_stop
};

static const bm_exec_ops_t stream_ops = {
    stream_init, stream_start_with_ready_block, mock_safe_stop
};

static const bm_exec_ops_t stream_wrong_owner_ops = {
    stream_init_wrong_owner, stream_start_with_ready_block, mock_safe_stop
};

static const bm_exec_ops_t stream_late_ops = {
    stream_init, stream_start_with_late_block, mock_safe_stop
};

static const bm_exec_ops_t stream_foreign_clock_ops = {
    stream_init, stream_start_with_foreign_clock_block, mock_safe_stop
};

static const bm_exec_ops_t stream_wrapped_late_ops = {
    stream_init, stream_start_with_wrapped_late_block, mock_safe_stop
};

static const bm_exec_ops_t stream_on_ready_ops = {
    stream_init_with_on_ready, mock_start, mock_safe_stop
};

static const bm_exec_slot_t bind_fail_slots[] = {
    {
        .kind = BM_EXEC_SLOT_HARDWARE,
        .run = hardware_step,
        .bind = bind_fail,
        .name = "bind_fail"
    },
};

static const bm_exec_slot_t invalid_hardware_period_slots[] = {
    {
        .kind = BM_EXEC_SLOT_HARDWARE,
        .period_us = 1u,
        .run = hardware_step,
        .bind = bind_adc,
        .name = "bad_hw_period"
    },
};

static const bm_exec_slot_t stream_slots[] = {
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = 1000u,
        .flags = BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE,
        .run_block = stream_block_step,
        .stream = &g_stream,
        .name = "stream"
    },
};

static const bm_exec_slot_t duplicate_stream_slots[] = {
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = 1000u,
        .flags = BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE,
        .run_block = stream_block_step,
        .stream = &g_stream,
        .name = "stream_a"
    },
    {
        .kind = BM_EXEC_SLOT_FRAME,
        .deadline_us = 1000u,
        .flags = BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE,
        .run_block = stream_block_step,
        .stream = &g_stream,
        .name = "stream_b"
    },
};

static const bm_exec_t fail_init_inst = {
    2u, 0u, "fail_init", NULL, NULL, NULL,
    scheduled_slots,
    (uint32_t)(sizeof(scheduled_slots) / sizeof(scheduled_slots[0])),
    NULL, 0u, &fail_init_ops
};

static const bm_exec_t fail_start_inst = {
    2u, 0u, "fail_start", NULL, NULL, NULL,
    scheduled_slots,
    (uint32_t)(sizeof(scheduled_slots) / sizeof(scheduled_slots[0])),
    NULL, 0u, &fail_start_ops
};

static const bm_exec_t bind_fail_inst = {
    3u, 0u, "bind_fail", NULL, NULL, NULL,
    bind_fail_slots,
    (uint32_t)(sizeof(bind_fail_slots) / sizeof(bind_fail_slots[0])),
    NULL, 0u, &fail_bind_ops
};

static const bm_exec_t invalid_hardware_period_inst = {
    4u, 0u, "bad_hw_period", NULL, NULL, NULL,
    invalid_hardware_period_slots,
    (uint32_t)(sizeof(invalid_hardware_period_slots) /
               sizeof(invalid_hardware_period_slots[0])),
    NULL, 0u, &mock_ops
};

static const bm_exec_t wrong_inst = {
    99u, 0u, "wrong", NULL, NULL, NULL,
    NULL, 0u, NULL, 0u, &wrong_ops
};

static const bm_exec_t stream_inst = {
    5u, 0u, "stream", NULL, NULL, NULL,
    stream_slots,
    (uint32_t)(sizeof(stream_slots) / sizeof(stream_slots[0])),
    NULL, 0u, &stream_ops
};

static const bm_exec_t stream_late_inst = {
    7u, 0u, "stream_late", NULL, NULL, NULL,
    stream_slots,
    (uint32_t)(sizeof(stream_slots) / sizeof(stream_slots[0])),
    NULL, 0u, &stream_late_ops
};

static const bm_exec_t stream_foreign_clock_inst = {
    8u, 0u, "stream_foreign", NULL, NULL, NULL,
    stream_slots,
    (uint32_t)(sizeof(stream_slots) / sizeof(stream_slots[0])),
    NULL, 0u, &stream_foreign_clock_ops
};

static const bm_exec_t stream_wrapped_late_inst = {
    9u, 0u, "stream_wrap", NULL, NULL, NULL,
    stream_slots,
    (uint32_t)(sizeof(stream_slots) / sizeof(stream_slots[0])),
    NULL, 0u, &stream_wrapped_late_ops
};

static const bm_exec_t duplicate_stream_inst = {
    6u, 0u, "duplicate_stream", NULL, NULL, NULL,
    duplicate_stream_slots,
    (uint32_t)(sizeof(duplicate_stream_slots) /
               sizeof(duplicate_stream_slots[0])),
    NULL, 0u, &stream_ops
};

static const bm_exec_t stream_on_ready_inst = {
    10u, 0u, "stream_on_ready", NULL, NULL, NULL,
    stream_slots,
    (uint32_t)(sizeof(stream_slots) / sizeof(stream_slots[0])),
    NULL, 0u, &stream_on_ready_ops
};

static const bm_exec_t foreign_stream_owner_inst = {
    10u, 1u, "foreign_stream", NULL, NULL, NULL,
    stream_slots,
    (uint32_t)(sizeof(stream_slots) / sizeof(stream_slots[0])),
    NULL, 0u, &stream_wrong_owner_ops
};

void setUp(void) {
    BM_LOGI("test_ctrl", "setUp: reset counters and HRT");
    g_step_count = 0u;
    g_hw_step_count = 0u;
    g_safe_stop_count = 0u;
    g_inst2_started = 0u;
    g_inst2_premature_steps = 0u;
    g_fire_hardware_during_stop = 0u;
    g_wrong_safe_stop_count = 0u;
    g_block_run_count = 0u;
    g_stream.on_ready = NULL;
    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);
    bm_hal_timer_native_reset_ticks();
    bm_hal_timer_native_set_init_result(BM_OK);
    bm_hrt_reset();
}

void tearDown(void) {
    bm_exec_safe_stop_all(NULL, 0u);
    BM_LOGI("test_ctrl", "tearDown: safe_stop_all done");
}

static int start_exec_runtime(const bm_exec_t *const *instances,
                              uint32_t count) {
    int rc = bm_exec_start_all(instances, count);

#if BM_CPU_LOCAL_ENABLE_ROUTE
    if (rc == BM_OK) {
        rc = bm_exec_irq_release_all();
    }
#endif
    return rc;
}

static void drain_exec_streams(void) {
    (void)bm_exec_drain_streams(UINT32_MAX);
}

void test_exec_init_and_scheduled_slot(void) {
    const bm_exec_t *const instances[] = { &sched_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, start_exec_runtime(instances, 1u));

    bm_hal_timer_native_advance_ticks(10u);
    TEST_ASSERT_GREATER_THAN(0u, g_step_count);

    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_hardware_bind_fires_step(void) {
    const bm_exec_t *const instances[] = { &hw_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
    TEST_ASSERT_EQUAL(0u, g_hw_step_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
    TEST_ASSERT_EQUAL(1u, g_hw_step_count);

    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_hardware_only_does_not_initialize_hrt(void) {
    const bm_exec_t *const instances[] = { &hw_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_hrt_start());
    bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
    TEST_ASSERT_EQUAL(0u, g_hw_step_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
    TEST_ASSERT_EQUAL(1u, g_hw_step_count);
    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_init_failure_rolls_back_safe_stop(void) {
    /* 第二实例 init 失败时，应回滚并调用已 init 实例的 safe_stop */
    const bm_exec_t *const instances[] = { &sched_inst, &fail_init_inst };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 2u));
    TEST_ASSERT_EQUAL(1u, g_safe_stop_count);
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(&instances[0], 1u));
    TEST_ASSERT_EQUAL(BM_OK, start_exec_runtime(&instances[0], 1u));
    bm_hal_timer_native_advance_ticks(10u);
    TEST_ASSERT_GREATER_THAN(0u, g_step_count);
    bm_exec_safe_stop_all(&instances[0], 1u);
}

void test_exec_start_failure_rolls_back_safe_stop(void) {
    const bm_exec_t *const instances[] = { &sched_inst, &fail_start_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 2u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_start_all(instances, 2u));
    TEST_ASSERT_EQUAL(2u, g_safe_stop_count);
}

void test_exec_start_all_starts_hrt_after_all_instances(void) {
    const bm_exec_t *const instances[] = {
        &sched_inst_advance, &sched_inst2
    };

    g_inst2_started = 0u;
    g_inst2_premature_steps = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 2u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 2u));
    TEST_ASSERT_EQUAL(0u, g_inst2_premature_steps);
    bm_exec_safe_stop_all(instances, 2u);
}

void test_exec_session_transitions(void) {
    const bm_exec_t *const instances[] = { &sched_inst };

    TEST_ASSERT_EQUAL(BM_EXEC_SESSION_NONE, bm_exec_get_session());
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_EXEC_SESSION_INITED, bm_exec_get_session());
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_EXEC_SESSION_STARTED, bm_exec_get_session());
    bm_exec_safe_stop_all(instances, 1u);
    TEST_ASSERT_EQUAL(BM_EXEC_SESSION_NONE, bm_exec_get_session());
}

void test_exec_start_all_without_init_fails(void) {
    const bm_exec_t *const instances[] = { &sched_inst };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_start_all(instances, 1u));
}

void test_exec_init_rejects_null_instance(void) {
    const bm_exec_t *const instances[] = { &sched_inst, NULL };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 2u));
}

void test_exec_rejects_double_start(void) {
    const bm_exec_t *const instances[] = { &sched_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_exec_start_all(instances, 1u));
    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_bind_failure_rolls_back(void) {
    const bm_exec_t *const instances[] = { &bind_fail_inst };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 1u));
    bm_hal_adc_sim_fire_complete(&BM_HAL_ADC_SIM0);
    TEST_ASSERT_EQUAL(0u, g_hw_step_count);
}

void test_exec_rejects_hardware_slot_with_period(void) {
    const bm_exec_t *const instances[] = {
        &invalid_hardware_period_inst
    };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 1u));
}

void test_exec_hrt_start_failure_keeps_session_closed(void) {
    const bm_exec_t *const instances[] = { &sched_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    bm_hal_timer_native_set_init_result(BM_ERR_BUSY);
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, start_exec_runtime(instances, 1u));
    TEST_ASSERT_EQUAL(BM_EXEC_SESSION_NONE, bm_exec_get_session());
    TEST_ASSERT_EQUAL(0, bm_hrt_is_started());
    TEST_ASSERT_EQUAL(1u, g_safe_stop_count);
}

void test_exec_find_instance(void) {
    const bm_exec_t *const instances[] = { &sched_inst, &hw_inst };

    TEST_ASSERT_NOT_NULL(bm_exec_find(instances, 2u, 2u));
    TEST_ASSERT_NULL(bm_exec_find(instances, 2u, 99u));
}

void test_exec_reinit_teardowns_previous_session(void) {
    const bm_exec_t *const instances[] = { &sched_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    g_safe_stop_count = 0u;
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(1u, g_safe_stop_count);
    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_stop_blocks_hardware_callback_before_safe_stop(void) {
    const bm_exec_t *const instances[] = { &hw_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    g_fire_hardware_during_stop = 1u;
    bm_exec_safe_stop_all(instances, 1u);
    TEST_ASSERT_EQUAL(0u, g_hw_step_count);
    TEST_ASSERT_EQUAL(1u, g_safe_stop_count);
}

void test_exec_safe_stop_uses_registered_instances_on_mismatch(void) {
    const bm_exec_t *const instances[] = { &hw_inst };
    const bm_exec_t *const wrong_instances[] = { &wrong_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    bm_exec_safe_stop_all(wrong_instances, 1u);
    TEST_ASSERT_EQUAL(1u, g_safe_stop_count);
    TEST_ASSERT_EQUAL(0u, g_wrong_safe_stop_count);
}

void test_exec_drains_block_committed_during_start(void) {
    const bm_exec_t *const instances[] = { &stream_inst };

    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    drain_exec_streams();
    TEST_ASSERT_EQUAL(1u, g_block_run_count);
    TEST_ASSERT_EQUAL(0u, bm_stream_ready_count(&g_stream));
}

void test_exec_rejects_multiple_slots_for_same_stream(void) {
    const bm_exec_t *const instances[] = { &duplicate_stream_inst };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 1u));
}

void test_exec_rejects_stream_owner_mismatch(void) {
    const bm_exec_t *const instances[] = { &foreign_stream_owner_inst };

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 1u));
}

void test_exec_rejects_on_ready_with_block_slot(void) {
    const bm_exec_t *const instances[] = { &stream_on_ready_inst };

    /*
     * 契约 3：绑定 exec block 槽的 stream 不得同时挂 on_ready 回调，否则单核
     * bm_stream_drain 与 exec_drain_streams 双消费者争用 READY 队列。exec 验证
     * 期须拒绝。按 CPU 路由启用时 on_ready 已在 set_ready_handler 处被拒，
     * 此误配路径不可达。
     */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_exec_init_all(instances, 1u));
}

void test_exec_block_deadline_drops_late_by_default(void) {
    const bm_exec_t *const instances[] = { &stream_late_inst };

    bm_hal_timer_native_advance_ticks(200u);
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    drain_exec_streams();
    TEST_ASSERT_EQUAL(0u, g_block_run_count);
    TEST_ASSERT_EQUAL(1u, bm_stream_stats(&g_stream)->late);
    TEST_ASSERT_EQUAL(0u, bm_stream_ready_count(&g_stream));
    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_block_deadline_skips_foreign_clock(void) {
    const bm_exec_t *const instances[] = { &stream_foreign_clock_inst };

    bm_hal_timer_native_advance_ticks(200u);
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    drain_exec_streams();
    TEST_ASSERT_EQUAL(0u, g_block_run_count);
    TEST_ASSERT_EQUAL(1u, bm_stream_stats(&g_stream)->late);
    bm_exec_safe_stop_all(instances, 1u);
}

void test_exec_block_deadline_handles_tick_wraparound(void) {
    const bm_exec_t *const instances[] = { &stream_wrapped_late_inst };

    bm_hal_timer_native_advance_ticks(32u);
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_init_all(instances, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_exec_start_all(instances, 1u));
    drain_exec_streams();
    TEST_ASSERT_EQUAL(0u, g_block_run_count);
    TEST_ASSERT_EQUAL(1u, bm_stream_stats(&g_stream)->late);
    TEST_ASSERT_EQUAL(0u, bm_stream_ready_count(&g_stream));
    bm_exec_safe_stop_all(instances, 1u);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_exec_init_and_scheduled_slot);
    RUN_TEST(test_exec_hardware_bind_fires_step);
    RUN_TEST(test_exec_hardware_only_does_not_initialize_hrt);
    RUN_TEST(test_exec_init_failure_rolls_back_safe_stop);
    RUN_TEST(test_exec_start_failure_rolls_back_safe_stop);
    RUN_TEST(test_exec_bind_failure_rolls_back);
    RUN_TEST(test_exec_rejects_hardware_slot_with_period);
    RUN_TEST(test_exec_hrt_start_failure_keeps_session_closed);
    RUN_TEST(test_exec_find_instance);
    RUN_TEST(test_exec_reinit_teardowns_previous_session);
    RUN_TEST(test_exec_start_all_starts_hrt_after_all_instances);
    RUN_TEST(test_exec_session_transitions);
    RUN_TEST(test_exec_start_all_without_init_fails);
    RUN_TEST(test_exec_init_rejects_null_instance);
    RUN_TEST(test_exec_rejects_double_start);
    RUN_TEST(test_exec_stop_blocks_hardware_callback_before_safe_stop);
    RUN_TEST(test_exec_safe_stop_uses_registered_instances_on_mismatch);
    RUN_TEST(test_exec_drains_block_committed_during_start);
    RUN_TEST(test_exec_rejects_multiple_slots_for_same_stream);
    RUN_TEST(test_exec_rejects_stream_owner_mismatch);
    RUN_TEST(test_exec_rejects_on_ready_with_block_slot);
    RUN_TEST(test_exec_block_deadline_drops_late_by_default);
    RUN_TEST(test_exec_block_deadline_skips_foreign_clock);
    RUN_TEST(test_exec_block_deadline_handles_tick_wraparound);
    return UNITY_END();
}

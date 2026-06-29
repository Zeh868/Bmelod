/**
 * @file test_mp.c
 * @brief BM_MP_PERCPU 分区与启动单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            stream gate 校验用例
 */
#include "unity.h"
#include "bm/mp/bm_mp.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_schedule.h"
#include "bm/mp/bm_mp_stream_gate.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm_event.h"
#include "bm_module.h"
#include "bm_hal_cpu_mp_native.h"
#include "bm_hal_timer_native.h"
#include "hal/bm_hal_timer.h"

#include <string.h>

static uint32_t g_module_init_cpu[BM_CONFIG_CPU_COUNT];
#if BM_CONFIG_CPU_COUNT > 1u && !BM_CONFIG_MP_HARD_RT_PROFILE
static uint32_t g_forwarded_event_count;
static uint8_t g_forwarded_event_source;
#endif

static int module_a_init(void) {
    g_module_init_cpu[bm_hal_cpu_id()]++;
    return BM_OK;
}

static int module_b_init(void) {
    g_module_init_cpu[bm_hal_cpu_id()]++;
    return BM_OK;
}

#if BM_CONFIG_CPU_COUNT > 1u && !BM_CONFIG_MP_HARD_RT_PROFILE
static void forwarded_event_cb(const bm_event_t *event, void *user_data) {
    (void)user_data;
    if (event != NULL && event->type == 1u) {
        g_forwarded_event_count++;
        g_forwarded_event_source = event->source_id;
    }
}
#endif

BM_MODULE_DEFINE(module_a, 0u, module_a_init, NULL, NULL, NULL);
BM_MODULE_DEFINE(module_b, 1u, module_b_init, NULL, NULL, NULL);
BM_MODULE_TABLE(BM_MODULE_ENTRY(module_a), BM_MODULE_ENTRY(module_b));

void setUp(void) {
    uint32_t cpu;

    bm_hal_cpu_init();
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        g_module_init_cpu[cpu] = 0u;
    }
#if BM_CONFIG_CPU_COUNT > 1u && !BM_CONFIG_MP_HARD_RT_PROFILE
    g_forwarded_event_count = 0u;
    g_forwarded_event_source = BM_CPU_ANY;
#endif
    bm_event_reset();
}

void tearDown(void) {
    uint32_t cpu;

    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        if (bm_hal_cpu_native_set_id(cpu) == BM_OK) {
            (void)bm_module_deinit_all();
        }
    }
    (void)bm_hal_cpu_native_set_id(0u);
}

void test_mp_partition_build_single_cpu(void) {
    bm_mp_partition_reset();
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_event_owner(0u, "event0", 0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_partition_build_and_validate());
    TEST_ASSERT_NOT_NULL(bm_mp_partition());
    TEST_ASSERT_EQUAL(0u, bm_mp_event_owner(0u));
}

void test_mp_boot_format_and_attach(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_init(BM_CONFIG_CPU_COUNT - 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
#if BM_MP_MULTICORE && BM_CONFIG_MP_HARD_RT_PROFILE
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_mp_boot_cpu_attach_and_init());
#else
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_cpu_attach_and_init());
#endif
}

void test_mp_failed_phase_is_terminal(void) {
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    bm_atomic_ipc_store_u32(
        &matrix->boot_phase, (uint32_t)BM_MP_BOOT_FAILED);
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_mp_boot_wait_matrix_phase(BM_MP_BOOT_PARTITION_READY, 1000u));
}

#if BM_CONFIG_CPU_COUNT > 1u
void test_mp_attach_rejects_missing_boot_epoch(void) {
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    bm_atomic_ipc_store_u32(&matrix->boot_epoch, 0u);
#if BM_CONFIG_MP_HARD_RT_PROFILE
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_mp_boot_cpu_attach_and_init());
#else
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_boot_cpu_attach_and_init());
    TEST_ASSERT_EQUAL(
        (uint32_t)BM_MP_BOOT_FAILED,
        bm_atomic_ipc_load_u32(&matrix->boot_phase));
#endif
}

void test_mp_irq_release_is_local_until_barrier(void) {
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);

    bm_mp_boot_release_irq();
    TEST_ASSERT_EQUAL(0, bm_mp_boot_is_irq_released());

    bm_atomic_ipc_store_u32(
        &matrix->cpu_ready[0], (uint32_t)BM_MP_BOOT_IRQ_RELEASE);
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(0, bm_mp_boot_is_irq_released());
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_barrier_wait(BM_MP_BOOT_IRQ_RELEASE, 10000u));
    TEST_ASSERT_EQUAL(1, bm_mp_boot_is_irq_released());
}

void test_mp_barrier_timeout_publishes_failure(void) {
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    TEST_ASSERT_EQUAL(
        BM_ERR_TIMEOUT,
        bm_mp_barrier_wait(BM_MP_BOOT_RUNTIME_READY, 1000u));
    TEST_ASSERT_EQUAL(
        (uint32_t)BM_MP_BOOT_FAILED,
        bm_atomic_ipc_load_u32(&matrix->boot_phase));
}

void test_module_runtime_state_is_per_cpu(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_on_this_cpu());
    TEST_ASSERT_EQUAL(1u, g_module_init_cpu[0]);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_module_init_on_this_cpu());
    TEST_ASSERT_EQUAL(1u, g_module_init_cpu[1]);
}

void test_event_owner_decl_and_forwarding(void) {
#if !BM_CONFIG_MP_HARD_RT_PROFILE
    bm_event_subscriber_id_t id = 0u;
    uint32_t value = 0x12345678u;
#endif

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_event_register_type_owner(1u, "forwarded", BM_CPU_ANY));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
    TEST_ASSERT_EQUAL(1u, bm_mp_event_owner(1u));

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
#if BM_CONFIG_MP_HARD_RT_PROFILE
    TEST_ASSERT_EQUAL(BM_ERR_NOT_INIT, bm_mp_boot_cpu_attach_and_init());
    return;
#else
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_cpu_attach_and_init());

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_cpu_attach_and_init());
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_event_subscribe(1u, forwarded_event_cb, NULL, &id));
    bm_event_freeze_subscriptions();

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_event_publish_copy(1u, 0u,
                              &value, sizeof(value)));

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(1, bm_mp_ipc_drain_on_this_cpu(1u));
    TEST_ASSERT_EQUAL(1, bm_event_process(1u));
    TEST_ASSERT_EQUAL(1u, g_forwarded_event_count);
    TEST_ASSERT_EQUAL_UINT8(0u, g_forwarded_event_source);
#endif
}

void test_ipc_event_source_seq_is_monotonic_across_ring_wrap(void) {
    bm_mp_ipc_matrix_t *matrix;
    bm_event_t event;
    uint32_t i;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    memset(&event, 0, sizeof(event));
    event.type = 0u;
    event.priority = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    for (i = 0u; i < BM_CONFIG_MP_IPC_EVENT_RING_DEPTH - 1u; i++) {
        TEST_ASSERT_EQUAL(
            BM_OK,
            bm_mp_ipc_publish_event_forward(1u, &event, NULL, 0u));
    }
    TEST_ASSERT_EQUAL(
        BM_CONFIG_MP_IPC_EVENT_RING_DEPTH - 1u,
        matrix->event_ring[0][1].slots[
            BM_CONFIG_MP_IPC_EVENT_RING_DEPTH - 2u].source_seq);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    bm_atomic_ipc_store_u32(&matrix->event_ring[0][1].tail.value,
                            BM_CONFIG_MP_IPC_EVENT_RING_DEPTH - 1u);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_ipc_publish_event_forward(1u, &event, NULL, 0u));
    TEST_ASSERT_EQUAL(
        BM_CONFIG_MP_IPC_EVENT_RING_DEPTH,
        matrix->event_ring[0][1].slots[BM_CONFIG_MP_IPC_EVENT_RING_DEPTH - 1u]
            .source_seq);
}

void test_ipc_event_source_seq_skips_zero_on_wrap(void) {
    bm_mp_ipc_matrix_t *matrix;
    bm_event_t event;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    memset(&event, 0, sizeof(event));
    event.type = 0u;
    event.priority = 0u;

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    bm_atomic_ipc_store_u32(&matrix->event_ring[0][1].next_source_seq,
                            UINT32_MAX);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_ipc_publish_event_forward(1u, &event, NULL, 0u));
    TEST_ASSERT_EQUAL(1u, matrix->event_ring[0][1].slots[0].source_seq);
}

void test_ipc_event_drain_counts_sequence_error(void) {
    bm_mp_ipc_matrix_t *matrix;
    bm_mp_ipc_event_ring_t *ring;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    ring = &matrix->event_ring[0][1];
    ring->slots[0].type = 0u;
    ring->slots[0].priority = 0u;
    ring->slots[0].source_id = 0u;
    ring->slots[0].data_len = 0u;
    ring->slots[0].source_seq = 0u;
    bm_atomic_ipc_store_u32(&ring->head.value, 1u);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
#if BM_CONFIG_MP_HARD_RT_PROFILE
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_ipc_drain_on_this_cpu(1u));
#else
    TEST_ASSERT_EQUAL(1, bm_mp_ipc_drain_on_this_cpu(1u));
#endif
    TEST_ASSERT_EQUAL(1u, bm_atomic_ipc_load_u32(&ring->sequence_errors));
}
#endif

void test_mp_main_loop_period_elapsed_boundary(void) {
    /* 1 MHz：1 tick = 1 us。周期 1000 us。 */
    TEST_ASSERT_EQUAL(0, bm_mp_main_loop_period_elapsed(0u, 999u, 1000000u, 1000u));
    TEST_ASSERT_EQUAL(1, bm_mp_main_loop_period_elapsed(0u, 1000u, 1000000u, 1000u));
    TEST_ASSERT_EQUAL(1, bm_mp_main_loop_period_elapsed(0u, 5000u, 1000000u, 1000u));
    /* period_us==0 或 freq==0 视为已满足（不强制 / 无时基）。 */
    TEST_ASSERT_EQUAL(1, bm_mp_main_loop_period_elapsed(0u, 0u, 1000000u, 0u));
    TEST_ASSERT_EQUAL(1, bm_mp_main_loop_period_elapsed(0u, 0u, 0u, 1000u));
}

void test_mp_main_loop_period_elapsed_is_wrap_safe(void) {
    uint32_t start = 0xFFFFFFFFu;

    /* 跨 uint32_t 回绕：now = start + 999 (=998)，elapsed=999us < 1000 → 未达。 */
    TEST_ASSERT_EQUAL(
        0, bm_mp_main_loop_period_elapsed(start, start + 999u, 1000000u, 1000u));
    /* now = start + 1000 (=999)，elapsed=1000us ≥ 1000 → 已达。 */
    TEST_ASSERT_EQUAL(
        1, bm_mp_main_loop_period_elapsed(start, start + 1000u, 1000000u, 1000u));
}

void test_mp_main_loop_overrun_count_starts_zero(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(0u, bm_mp_main_loop_overrun_count());
}

static uint32_t g_iter_hook_calls;

static void counting_iter_hook(void) {
    g_iter_hook_calls++;
}

/*
 * 直接执行一轮主迭代体（stream/relay/IPC drain → ticker → event process →
 * log drain → iter hook → wdg feed），覆盖 bm_mp_cpu_main_iteration；同时验证
 * iter hook 的越界忽略与“迭代后冻结”语义。
 */
void test_mp_cpu_main_iteration_runs_and_freezes_hook(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));

    g_iter_hook_calls = 0u;
    bm_mp_set_cpu_iter_hook(0u, counting_iter_hook);
    bm_mp_set_cpu_iter_hook(BM_CONFIG_CPU_COUNT,
                            counting_iter_hook); /* 越界忽略 */

    bm_mp_cpu_main_iteration();
    TEST_ASSERT_EQUAL(1u, g_iter_hook_calls);
    TEST_ASSERT_EQUAL(0u, bm_mp_main_loop_overrun_count());

    /* 迭代后 hook 注册被冻结：尝试清空应被忽略，下一轮仍调用既有 hook。 */
    bm_mp_set_cpu_iter_hook(0u, NULL);
    bm_mp_cpu_main_iteration();
    TEST_ASSERT_EQUAL(2u, g_iter_hook_calls);
}

#if BM_CONFIG_CPU_COUNT > 1u
static uint32_t g_ipc_fault_hook_calls;
static uint8_t g_ipc_fault_src;
static uint8_t g_ipc_fault_dst;

static void counting_ipc_fault_hook(uint8_t source_cpu, uint8_t target_cpu) {
    g_ipc_fault_hook_calls++;
    g_ipc_fault_src = source_cpu;
    g_ipc_fault_dst = target_cpu;
}

void test_ipc_fault_hook_fires_on_sequence_error(void) {
    bm_mp_ipc_matrix_t *matrix;
    bm_mp_ipc_event_ring_t *ring;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);

    g_ipc_fault_hook_calls = 0u;
    g_ipc_fault_src = BM_CPU_ANY;
    g_ipc_fault_dst = BM_CPU_ANY;
    bm_mp_ipc_set_fault_hook(counting_ipc_fault_hook);

    ring = &matrix->event_ring[0][1];
    ring->slots[0].type = 0u;
    ring->slots[0].priority = 0u;
    ring->slots[0].source_id = 0u;
    ring->slots[0].data_len = 0u;
    ring->slots[0].source_seq = 0u;   /* 0 = 序列异常哨兵 */
    bm_atomic_ipc_store_u32(&ring->head.value, 1u);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
#if BM_CONFIG_MP_HARD_RT_PROFILE
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_ipc_drain_on_this_cpu(1u));
#else
    TEST_ASSERT_EQUAL(1, bm_mp_ipc_drain_on_this_cpu(1u));
#endif
    TEST_ASSERT_EQUAL(1u, g_ipc_fault_hook_calls);
    TEST_ASSERT_EQUAL_UINT8(0u, g_ipc_fault_src);
    TEST_ASSERT_EQUAL_UINT8(1u, g_ipc_fault_dst);

    bm_mp_ipc_set_fault_hook(NULL);
}
#endif

void test_mp_schedule_validate_single_cpu(void) {
    bm_mp_schedule_slot_t slot = {
        .name = "tick",
        .owner_cpu = 0u,
        .wcet_us = 50u,
        .period_us = 1000u,
        .deadline_us = 1000u,
        .blocking_us = 10u
    };

    bm_mp_schedule_reset();
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_partition_validate_schedule(0u, NULL));
    bm_mp_schedule_print_report();
}

void test_mp_schedule_rejects_response_over_deadline(void) {
    bm_mp_schedule_slot_t slot = {
        "late", 0u, 80u, 1000u, 100u, 20u, 10u,
        0u, 0u, 0u, 0u, 0u
    };

    bm_mp_schedule_reset();
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL(
        BM_ERR_OVERFLOW,
        bm_mp_partition_validate_schedule(0u, NULL));
}

void test_mp_schedule_rejects_stream_slot_without_overhead_budget(void) {
    bm_mp_schedule_slot_t slot = {
        "stream", 0u, 50u, 1000u, 1000u, 10u, 5u,
        BM_MP_SCHEDULE_FLAG_STREAM, 0u, 0u, 0u, 0u
    };

    bm_mp_schedule_reset();
#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_schedule_register(&slot));
#else
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_mp_partition_validate_schedule(0u, NULL));
#endif
}

void test_mp_schedule_accounts_stream_and_relay_overheads(void) {
    bm_mp_schedule_slot_t slot = {
        "relay", 0u, 50u, 1000u, 200u, 10u, 5u,
        BM_MP_SCHEDULE_FLAG_STREAM | BM_MP_SCHEDULE_FLAG_RELAY,
        20u, 15u, 30u, 5u
    };

    bm_mp_schedule_reset();
#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_schedule_register(&slot));
#else
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_validate_schedule(0u, NULL));
#endif
}

void test_mp_stream_gate_derives_scan_and_depth(void) {
    bm_mp_stream_gate_params_t params = {
        4u, 1000u, 120u, 5000u, 200u, 0u, 2u, 0u, 0u
    };
    bm_mp_stream_gate_report_t report;

    TEST_ASSERT_EQUAL(8u, bm_mp_stream_derive_scan_us(4u, 2u));
    TEST_ASSERT_EQUAL(2u, bm_mp_stream_derive_min_depth(&params, &report));
    TEST_ASSERT_EQUAL(8u, report.derived_scan_us);
    TEST_ASSERT_EQUAL(1, report.service_sustainable);
}

void test_mp_stream_gate_rejects_unsustainable_service(void) {
    bm_mp_stream_gate_params_t params = {
        4u, 1000u, 1200u, 5000u, 200u, 0u, 2u, 0u, 0u
    };

    TEST_ASSERT_EQUAL(0u, bm_mp_stream_derive_min_depth(&params, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_stream_validate_gate(&params, NULL));
}

void test_mp_stream_gate_accounts_stream_overhead_in_service_rate(void) {
    bm_mp_stream_gate_params_t params = {
        4u, 1000u, 980u, 5000u, 200u, 0u, 2u, 5u, 10u
    };

    TEST_ASSERT_EQUAL(0u, bm_mp_stream_derive_min_depth(&params, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_stream_validate_gate(&params, NULL));
}

void test_mp_stream_gate_register_fills_schedule_overhead(void) {
    bm_mp_stream_gate_params_t gate = {
        4u, 1000u, 120u, 5000u, 200u, 0u, 2u, 5u, 10u
    };
    bm_mp_schedule_slot_t slot = {
        "stream_gate", 0u, 120u, 1000u, 5000u, 10u, 5u,
        BM_MP_SCHEDULE_FLAG_STREAM, 0u, 0u, 0u, 0u
    };

    bm_mp_schedule_reset();
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register_stream(&slot, &gate));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_partition_validate_schedule(0u, NULL));
}

void test_mp_schedule_utilization_accounts_stream_overhead(void) {
    bm_mp_schedule_slot_t slot = {
        "stream_util", 0u, 400u, 1000u, 1000u, 0u, 0u,
        BM_MP_SCHEDULE_FLAG_STREAM, 100u, 100u, 0u, 100u
    };

    bm_mp_schedule_reset();
#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_schedule_register(&slot));
#else
    bm_mp_schedule_cpu_report_t report;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_partition_validate_schedule(0u, &report));
    TEST_ASSERT_EQUAL(700000u, report.utilization_ppm);
#endif
}

void test_mp_schedule_rejects_stream_overhead_util_overload(void) {
    bm_mp_schedule_slot_t slot = {
        "stream_overload", 0u, 800u, 1000u, 2000u, 0u, 0u,
        BM_MP_SCHEDULE_FLAG_STREAM, 100u, 100u, 0u, 100u
    };

    bm_mp_schedule_reset();
#if BM_CONFIG_MP_HARD_RT_PROFILE && BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_schedule_register(&slot));
#else
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
                      bm_mp_partition_validate_schedule(0u, NULL));
#endif
}

void test_mp_ipc_cmd_snapshot_matrix(void) {
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
    TEST_ASSERT_EQUAL(BM_MP_IPC_LAYOUT_VERSION, matrix->layout_version);

    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_ipc_publish_cmd_snapshot(1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_ipc_cmd_snapshot_read(0u, NULL, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_WOULD_BLOCK,
                      bm_mp_ipc_cmd_snapshot_read(0u, NULL, NULL));
}

void test_mp_partition_rejects_module_event_owner_mismatch(void) {
#if BM_CONFIG_CPU_COUNT > 1u
    bm_mp_partition_reset();
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_event_owner(1u, "evt1", 1u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_module_event(0u, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_partition_build_and_validate());
    TEST_ASSERT_NULL(bm_mp_partition());
    TEST_ASSERT_EQUAL_UINT8(BM_CPU_ANY, bm_mp_event_owner(1u));
#endif
}

void test_mp_partition_recovers_after_failed_build(void) {
#if BM_CONFIG_CPU_COUNT > 1u
    bm_mp_partition_reset();
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_event_owner(1u, "evt1", 1u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_module_event(0u, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_partition_build_and_validate());
    TEST_ASSERT_NULL(bm_mp_partition());

    bm_mp_partition_reset();
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_event_owner(0u, "evt0", 0u));
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_partition_register_module_event(0u, 0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_partition_build_and_validate());
    TEST_ASSERT_NOT_NULL(bm_mp_partition());
    TEST_ASSERT_EQUAL_UINT8(0u, bm_mp_event_owner(0u));
    TEST_ASSERT_EQUAL_UINT8(1u, bm_mp_module_owner(1u));
#endif
}

void test_mp_main_loop_spin_bridge_matches_canonical_config(void) {
    TEST_ASSERT_EQUAL_UINT32(BM_CONFIG_MAIN_LOOP_MAX_SPINS,
                             BM_CONFIG_MP_MAIN_LOOP_MAX_SPINS);
}

void test_mp_schedule_snapshot_restore_is_repeatable(void) {
    bm_mp_schedule_slot_t slot = {
        .name = "snapshot",
        .owner_cpu = 0u,
        .wcet_us = 1u,
        .period_us = 1000u,
        .deadline_us = 1000u
    };
    uint32_t mark;

    bm_mp_schedule_reset();
    mark = bm_mp_schedule_mark();
    TEST_ASSERT_EQUAL_UINT32(0u, mark);
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
    TEST_ASSERT_EQUAL_UINT32(1u, bm_mp_schedule_mark());
    bm_mp_schedule_restore(mark);
    TEST_ASSERT_EQUAL_UINT32(mark, bm_mp_schedule_mark());
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_schedule_register(&slot));
}

void test_mp_watchdog_uses_configured_timeout(void) {
#if BM_CONFIG_CPU_COUNT > 1u
    uint32_t cpu;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_timer_init(1000u));
    bm_hal_timer_native_reset_ticks();

    for (cpu = 0u; cpu < BM_CONFIG_CPU_COUNT; cpu++) {
        TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(cpu));
        bm_mp_wdg_feed_this_cpu();
    }
    TEST_ASSERT_EQUAL(BM_OK, bm_hal_cpu_native_set_id(0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_wdg_bootstrap_check());

    bm_hal_timer_native_advance_ticks(
        BM_CONFIG_MP_WDG_HB_TIMEOUT_MS - 1u);
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_wdg_bootstrap_check());
    bm_hal_timer_native_advance_ticks(1u);
    TEST_ASSERT_EQUAL(BM_ERR_TIMEOUT, bm_mp_wdg_bootstrap_check());
#else
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_wdg_bootstrap_check());
#endif
}

#if BM_CONFIG_MP_HARD_RT_PROFILE
void test_mp_hard_rt_profile_publishes_ready_after_build(void) {
#if BM_CONFIG_ENABLE_EXEC
    static const bm_exec_t exec = {
        .id = 1u,
        .owner_cpu = 0u,
        .name = "profile_probe"
    };
    static const bm_exec_t *const execs[] = { &exec };
#endif
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
#if BM_CONFIG_ENABLE_EXEC
    bm_mp_profile_register_exec(execs, 1u);
#endif
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_profile_build());
    TEST_ASSERT_EQUAL(1, bm_mp_profile_is_built());
    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
#if BM_MP_MULTICORE
    TEST_ASSERT_EQUAL(
        (uint32_t)BM_MP_BOOT_PROFILE_READY,
        bm_atomic_ipc_load_u32(&matrix->boot_phase));
#endif
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_mp_profile_build());
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mp_partition_build_single_cpu);
    RUN_TEST(test_mp_boot_format_and_attach);
    RUN_TEST(test_mp_failed_phase_is_terminal);
    RUN_TEST(test_mp_main_loop_period_elapsed_boundary);
    RUN_TEST(test_mp_main_loop_period_elapsed_is_wrap_safe);
    RUN_TEST(test_mp_main_loop_overrun_count_starts_zero);
    RUN_TEST(test_mp_cpu_main_iteration_runs_and_freezes_hook);
    RUN_TEST(test_mp_schedule_validate_single_cpu);
    RUN_TEST(test_mp_schedule_rejects_response_over_deadline);
    RUN_TEST(test_mp_schedule_rejects_stream_slot_without_overhead_budget);
    RUN_TEST(test_mp_schedule_accounts_stream_and_relay_overheads);
    RUN_TEST(test_mp_stream_gate_derives_scan_and_depth);
    RUN_TEST(test_mp_stream_gate_rejects_unsustainable_service);
    RUN_TEST(test_mp_stream_gate_accounts_stream_overhead_in_service_rate);
    RUN_TEST(test_mp_stream_gate_register_fills_schedule_overhead);
    RUN_TEST(test_mp_schedule_utilization_accounts_stream_overhead);
    RUN_TEST(test_mp_schedule_rejects_stream_overhead_util_overload);
    RUN_TEST(test_mp_partition_rejects_module_event_owner_mismatch);
    RUN_TEST(test_mp_partition_recovers_after_failed_build);
    RUN_TEST(test_mp_main_loop_spin_bridge_matches_canonical_config);
    RUN_TEST(test_mp_schedule_snapshot_restore_is_repeatable);
    RUN_TEST(test_mp_watchdog_uses_configured_timeout);
#if BM_CONFIG_MP_HARD_RT_PROFILE
    RUN_TEST(test_mp_hard_rt_profile_publishes_ready_after_build);
#endif
#if BM_CONFIG_CPU_COUNT > 1u
    RUN_TEST(test_mp_ipc_cmd_snapshot_matrix);
    RUN_TEST(test_mp_attach_rejects_missing_boot_epoch);
    RUN_TEST(test_mp_irq_release_is_local_until_barrier);
    RUN_TEST(test_mp_barrier_timeout_publishes_failure);
    RUN_TEST(test_module_runtime_state_is_per_cpu);
    RUN_TEST(test_event_owner_decl_and_forwarding);
    RUN_TEST(test_ipc_event_source_seq_is_monotonic_across_ring_wrap);
    RUN_TEST(test_ipc_event_source_seq_skips_zero_on_wrap);
    RUN_TEST(test_ipc_event_drain_counts_sequence_error);
    RUN_TEST(test_ipc_fault_hook_fires_on_sequence_error);
#endif
    return UNITY_END();
}

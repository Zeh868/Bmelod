/**
 * @file main.c
 * @brief 双核双 bm_stream + relay 跨核块转发 Demo
 */
#include "bm_event.h"
#include "bm_exec.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_stream.h"
#include "bm/hybrid/bm_stream_relay.h"
#include "bm/mp/bm_mp.h"
#include "bm/mp/bm_mp_schedule.h"
#include "bm/mp/bm_mp_stream_gate.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm/mp/bm_mp_wdg.h"
#include "bm/mp/bm_mp_boot.h"
#include "bm_ticker.h"
#include "bm_wdg.h"
#include "hybrid_print.h"
#include "example_support.h"

#include "hal/bm_hal_cpu.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif

#include "bm/hybrid/bm_timestamp.h"
#include "bm/common/bm_atomic_ipc.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "bm_module.h"

const bm_module_t *_bm_module_table[1] = { NULL };
const uint32_t _bm_module_count = 0u;

#define TAG                    "dual_stream"
#define SAMPLES_PER_BLOCK      16u
#define BLOCK_PERIOD_US        1000u
#define SAMPLE_RATE_HZ         16000u
#define SINE_FREQ_HZ           440.0f
#define TWO_PI                 6.283185307f
#define DEMO_LOOPS             4000u
#define PASS_BLOCKS_PER_CPU    15u

#define BOOT_FAIL(step, code) do { \
    hybrid_print("EXAMPLE_DUAL_CORE_STREAM: FAIL "); \
    hybrid_print(step); \
    hybrid_print(" rc="); \
    example_print_u32((uint32_t)(-(code))); \
    hybrid_print("\n"); \
    return (code); \
} while (0)

#ifdef BM_EXAMPLE_QEMU_SMP
/*
 * QEMU 主循环周期为 10ms（见 bm_config_qemu_smp.h）；块 deadline 须与之对齐，
 * 否则 hard RT safe-stop 在首次迟到块即停机。
 */
#undef BLOCK_PERIOD_US
#define BLOCK_PERIOD_US        10000u
#undef DEMO_LOOPS
#define DEMO_LOOPS             400u
#define BLOCK_DEADLINE_US      50000u
#elif defined(NATIVE_SIM)
/*
 * native_sim：块周期须大于 mp_main_loop WCET（约 920us），否则 schedule
 * RTA 校验在 profile build 阶段返回 BM_ERR_OVERFLOW。
 */
#undef BLOCK_PERIOD_US
#define BLOCK_PERIOD_US        4000u
#define BLOCK_DEADLINE_US      BLOCK_PERIOD_US
#else
#define BLOCK_DEADLINE_US      BLOCK_PERIOD_US
#endif

typedef struct {
    float samples[SAMPLES_PER_BLOCK];
} pcm_block_t;

typedef struct {
    float    rms;
    uint32_t sequence;
} relay_summary_t;

typedef struct {
    float    phase;
    uint32_t produced;
    uint32_t processed;
    float    last_rms;
    int      enabled;
} axis_state_t;

BM_STREAM_PAYLOADS(g_stream0, pcm_block_t, 4u);
BM_STREAM_BLOCKS(g_stream0, 4u);
BM_STREAM_INSTANCE(g_stream0, 4u);

BM_STREAM_PAYLOADS(g_stream1, pcm_block_t, 4u);
BM_STREAM_BLOCKS(g_stream1, 4u);
BM_STREAM_INSTANCE(g_stream1, 4u);

static void streams_bind_owners(void) {
    g_stream0.owner_cpu = 0u;
    g_stream1.owner_cpu = 1u;
}

BM_STREAM_RELAY_SLOTS(g_relay_slots, 4u, sizeof(relay_summary_t));
static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) bm_stream_relay_t g_relay = {
    .source_cpu = 0u,
    .target_cpu = 1u,
    .slot_bytes = (uint32_t)sizeof(relay_summary_t),
    .depth = 4u,
    .slots = _bm_stream_relay_slots_g_relay_slots,
    .cache_policy = BM_STREAM_RELAY_CACHE_NON_CACHEABLE
};

static axis_state_t g_axis0;
static axis_state_t g_axis1;
static bm_atomic_ipc_u32_t g_relay_done_ipc = BM_ATOMIC_IPC_U32_INIT(0u);
static bm_atomic_ipc_u32_t g_axis1_produced_ipc = BM_ATOMIC_IPC_U32_INIT(0u);
static bm_atomic_ipc_u32_t g_axis1_count_ipc = BM_ATOMIC_IPC_U32_INIT(0u);

#ifdef BM_EXAMPLE_QEMU_SMP
/**
 * @brief 通知 QEMU virt 测试设备结束仿真
 *
 * @param passed 非零表示测试通过
 */
static void qemu_test_finish(int passed) {
    volatile uint32_t *test = (volatile uint32_t *)0x100000UL;

    *test = passed ? 0x5555u : 0x3333u;
}
#endif

static float block_rms(const bm_block_t *block) {
    const pcm_block_t *pcm;
    uint32_t n;
    uint32_t i;
    float sum = 0.0f;

    if (!block || !block->data || block->valid_bytes == 0u) {
        return 0.0f;
    }
    pcm = (const pcm_block_t *)block->data;
    n = block->valid_bytes / (uint32_t)sizeof(float);
    if (n > SAMPLES_PER_BLOCK) {
        n = SAMPLES_PER_BLOCK;
    }
    for (i = 0u; i < n; i++) {
        float s = pcm->samples[i];
        sum += s * s;
    }
    return (n == 0u) ? 0.0f : sqrtf(sum / (float)n);
}

static void fill_sine(axis_state_t *axis, pcm_block_t *pcm) {
    float step = TWO_PI * SINE_FREQ_HZ / (float)SAMPLE_RATE_HZ;
    uint32_t i;

    for (i = 0u; i < SAMPLES_PER_BLOCK; i++) {
        pcm->samples[i] = 0.5f * sinf(axis->phase);
        axis->phase += step;
        if (axis->phase >= TWO_PI) {
            axis->phase -= TWO_PI;
        }
    }
}

static void fill_block_timestamp(bm_timestamp_t *ts) {
    bm_hal_timer_handle_t timer = bm_hal_timer_for_cpu(bm_hal_cpu_id());

    ts->clock_id = timer.clock_id;
    ts->clock_epoch = timer.clock_epoch;
    ts->quality = 1u;
    ts->rate_hz = bm_hal_timer_get_freq();
    ts->ticks = bm_hal_timer_get_ticks();
}

static void cpu0_producer(const bm_exec_t *inst) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    pcm_block_t *pcm;
    bm_block_t *block;
    bm_timestamp_t ts;

    if (!axis || !axis->enabled) {
        return;
    }
    if (bm_stream_producer_acquire(&g_stream0, &block) != BM_OK) {
        return;
    }
    pcm = (pcm_block_t *)block->data;
    fill_sine(axis, pcm);
    fill_block_timestamp(&ts);
    if (bm_stream_producer_commit(&g_stream0, block,
                                  SAMPLES_PER_BLOCK * (uint32_t)sizeof(float),
                                  &ts) == BM_OK) {
        axis->produced++;
    }
}

static void cpu0_consumer(const bm_exec_t *inst, bm_block_t *block) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    relay_summary_t summary;

    axis->processed++;
    axis->last_rms = block_rms(block);
    summary.rms = axis->last_rms;
    summary.sequence = axis->processed;
    (void)bm_stream_relay_publish(&g_relay, &summary, sizeof(summary));
}

static void cpu1_producer(const bm_exec_t *inst) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    pcm_block_t *pcm;
    bm_block_t *block;
    bm_timestamp_t ts;

    if (!axis || !axis->enabled) {
        return;
    }
    if (bm_stream_producer_acquire(&g_stream1, &block) != BM_OK) {
        return;
    }
    pcm = (pcm_block_t *)block->data;
    fill_sine(axis, pcm);
    fill_block_timestamp(&ts);
    if (bm_stream_producer_commit(&g_stream1, block,
                                  SAMPLES_PER_BLOCK * (uint32_t)sizeof(float),
                                  &ts) == BM_OK) {
        axis->produced++;
        bm_atomic_ipc_store_u32(&g_axis1_produced_ipc, axis->produced);
    }
}

static void cpu1_consumer(const bm_exec_t *inst, bm_block_t *block) {
    axis_state_t *axis = (axis_state_t *)inst->state;

    axis->processed++;
    axis->last_rms = block_rms(block);
    bm_atomic_ipc_store_u32(&g_axis1_count_ipc, axis->processed);
}

static void relay_consume(bm_stream_relay_t *relay, const void *payload,
                          uint32_t len, uint32_t sequence, void *context) {
    const relay_summary_t *summary = (const relay_summary_t *)payload;
    (void)relay;
    (void)len;
    (void)context;
    if ((summary->sequence >= 5u || sequence >= 5u) && summary->rms > 0.1f) {
        bm_atomic_ipc_store_u32(&g_relay_done_ipc, 1u);
    }
}

static const bm_exec_slot_t g_cpu0_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = BLOCK_PERIOD_US,
        .run = cpu0_producer,
        .name = "cpu0_prod"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = BLOCK_DEADLINE_US,
        .flags = BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE,
        .run_block = cpu0_consumer,
        .stream = &g_stream0,
        .name = "cpu0_cons"
    }
};

static const bm_exec_slot_t g_cpu1_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = BLOCK_PERIOD_US,
        .run = cpu1_producer,
        .name = "cpu1_prod"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = BLOCK_DEADLINE_US,
        .flags = BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE,
        .run_block = cpu1_consumer,
        .stream = &g_stream1,
        .name = "cpu1_cons"
    }
};

static int axis_init(const bm_exec_t *inst) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    bm_stream_t *stream;
    void *payloads;

    if (!axis) {
        return BM_ERR_INVALID;
    }
    memset(axis, 0, sizeof(*axis));
    axis->enabled = 1;
    if (inst->owner_cpu == 0u) {
        stream = &g_stream0;
        payloads = _bm_stream_payload_g_stream0;
        stream->owner_cpu = 0u;
    } else {
        stream = &g_stream1;
        payloads = _bm_stream_payload_g_stream1;
        stream->owner_cpu = 1u;
    }
    return bm_stream_init(stream, payloads, 4u, sizeof(pcm_block_t));
}

static int axis_start(const bm_exec_t *inst) {
    (void)inst;
    return BM_OK;
}

static void axis_safe_stop(const bm_exec_t *inst) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    if (axis) {
        axis->enabled = 0;
    }
}

static const bm_exec_ops_t g_axis_ops = {
    axis_init, axis_start, axis_safe_stop
};

static const bm_exec_t g_exec_cpu0 = {
    1u, 0u, "cpu0_stream", &g_axis0, NULL, NULL,
    g_cpu0_slots, 2u, NULL, 0u, &g_axis_ops
};

static const bm_exec_t g_exec_cpu1 = {
    2u, 1u, "cpu1_stream", &g_axis1, NULL, NULL,
    g_cpu1_slots, 2u, NULL, 0u, &g_axis_ops
};

static const bm_exec_t *const g_instances[] = {
    &g_exec_cpu0, &g_exec_cpu1
};

static int cpu0_init_hook(void) {
    int rc;
    uint32_t boot_epoch = bm_mp_boot_epoch();

    if (boot_epoch != 0u) {
        g_relay.boot_epoch = boot_epoch;
    }
    rc = bm_stream_relay_init(&g_relay);
    if (rc != BM_OK) {
        BOOT_FAIL("cpu0_stream_relay_init", rc);
    }
    rc = bm_exec_init_on_this_cpu(g_instances, 2u);
    if (rc != BM_OK) {
        BOOT_FAIL("cpu0_exec_init", rc);
    }
    rc = bm_exec_prepare_on_this_cpu(g_instances, 2u);
    if (rc != BM_OK) {
        BOOT_FAIL("cpu0_exec_prepare", rc);
    }
    return BM_OK;
}

static int cpu1_init_hook(void) {
    int rc = bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    if (rc != BM_OK) {
        BOOT_FAIL("cpu1_timer_init", rc);
    }
    bm_mp_profile_bind_epoch_on_this_cpu();
    rc = bm_exec_init_on_this_cpu(g_instances, 2u);

    if (rc != BM_OK) {
        BOOT_FAIL("cpu1_exec_init", rc);
    }
    rc = bm_exec_prepare_on_this_cpu(g_instances, 2u);
    if (rc != BM_OK) {
        BOOT_FAIL("cpu1_exec_prepare", rc);
    }
    {
        uint32_t tries;

        for (tries = 0u; tries < 1000000u; tries++) {
            rc = bm_stream_relay_register_on_this_cpu(
                &g_relay, relay_consume, NULL);
            if (rc == BM_OK) {
                return BM_OK;
            }
            if (rc != BM_ERR_NOT_INIT) {
                BOOT_FAIL("cpu1_relay_register", rc);
            }
            bm_hal_cpu_yield();
        }
        return BM_ERR_TIMEOUT;
    }
}

#ifdef BM_EXAMPLE_QEMU_SMP
extern void qemu_rv64_smp_on_timer_irq(void);

/**
 * @brief QEMU 协作定时：绕过 exec deadline 检查直接 drain Block 槽
 */
static void qemu_exec_drain_blocks(const bm_exec_t *inst,
                                  const bm_exec_slot_t *slots,
                                  uint32_t slot_count) {
    uint32_t s;

    for (s = 0u; s < slot_count; s++) {
        const bm_exec_slot_t *slot = &slots[s];

        if (slot->kind != BM_EXEC_SLOT_BLOCK || !slot->stream ||
            !slot->run_block) {
            continue;
        }
        while (1) {
            bm_block_t *block;

            if (bm_stream_consumer_acquire(slot->stream, &block) != BM_OK) {
                break;
            }
            slot->run_block(inst, block);
            (void)bm_stream_consumer_release(slot->stream, block);
        }
    }
}
#endif

#ifdef BM_EXAMPLE_QEMU_SMP
/**
 * @brief QEMU 协作定时：在 exec deadline 检查前推进 tick 并 drain Block 槽
 */
void bm_mp_coop_pre_iteration(uint32_t cpu) {
    qemu_rv64_smp_on_timer_irq();
    if (cpu == 0u) {
        qemu_exec_drain_blocks(&g_exec_cpu0, g_cpu0_slots,
                               (uint32_t)(sizeof(g_cpu0_slots) /
                                          sizeof(g_cpu0_slots[0])));
    } else if (cpu == 1u) {
        qemu_exec_drain_blocks(&g_exec_cpu1, g_cpu1_slots,
                               (uint32_t)(sizeof(g_cpu1_slots) /
                                          sizeof(g_cpu1_slots[0])));
        (void)bm_stream_relay_drain_on_this_cpu(
            BM_CONFIG_MP_RELAY_DRAIN_BUDGET);
    }
}
#endif

static void cpu1_iter_hook(void) {
#ifdef NATIVE_SIM
    bm_hal_timer_native_advance_ticks(1u);
#endif
}

static void register_schedule(void) {
    static const bm_mp_stream_gate_params_t stream_gate = {
        4u, BLOCK_PERIOD_US, 120u, 5000u, 200u, 0u, 2u, 5u, 10u
    };
    static const bm_mp_schedule_slot_t slots[] = {
        { "cpu0_prod", 0u, 80u, BLOCK_PERIOD_US, BLOCK_PERIOD_US, 20u, 5u,
          0u, 0u, 0u, 0u, 0u },
        { "cpu0_cons", 0u, 120u, BLOCK_PERIOD_US, 5000u, 30u, 10u,
          BM_MP_SCHEDULE_FLAG_STREAM, 0u, 0u, 0u, 0u },
        { "cpu1_prod", 1u, 80u, BLOCK_PERIOD_US, BLOCK_PERIOD_US, 20u, 5u,
          0u, 0u, 0u, 0u, 0u },
        { "cpu1_cons", 1u, 150u, BLOCK_PERIOD_US, 5000u, 40u, 15u,
          BM_MP_SCHEDULE_FLAG_STREAM, 0u, 0u, 0u, 0u },
        { "relay_drain", 1u, 40u, BLOCK_PERIOD_US, BLOCK_PERIOD_US, 10u, 5u,
          BM_MP_SCHEDULE_FLAG_RELAY, 0u, 0u, 20u, 10u },
    };
    uint32_t i;

    bm_mp_schedule_reset();
    for (i = 0u; i < (sizeof(slots) / sizeof(slots[0])); i++) {
        if ((slots[i].flags & BM_MP_SCHEDULE_FLAG_STREAM) != 0u) {
            (void)bm_mp_schedule_register_stream(&slots[i], &stream_gate);
        } else {
            (void)bm_mp_schedule_register(&slots[i]);
        }
    }
}

static void mp_safe_stop(void) {
    BM_LOGE(TAG, "mp watchdog safe-stop");
}

static int bootstrap_mp(void) {
    int rc;

    bm_mp_set_cpu_init_hook(1u, cpu1_init_hook);
    bm_mp_set_cpu_iter_hook(1u, cpu1_iter_hook);
    bm_mp_wdg_set_safe_stop_hook(mp_safe_stop);

    rc = bm_mp_init(BM_CONFIG_CPU_COUNT);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: mp_init rc=%d", rc);
        BOOT_FAIL("mp_init", rc);
    }
#ifdef NATIVE_SIM
    bm_mp_set_demo_max_loops(DEMO_LOOPS + 2000u);
#else
    bm_mp_set_demo_max_loops(0u);
#endif
    rc = bm_hal_cpu_boot_secondary((uintptr_t)bm_mp_cpu_secondary_entry);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: boot_secondary rc=%d", rc);
        BOOT_FAIL("boot_secondary", rc);
    }
    rc = bm_mp_boot_bootstrap_sequence();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: bootstrap_sequence rc=%d", rc);
        BOOT_FAIL("bootstrap_sequence", rc);
    }
    register_schedule();
    bm_mp_schedule_print_report();
    bm_mp_profile_register_exec(g_instances, 2u);
    rc = bm_mp_profile_build();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: profile build rc=%d", rc);
        BOOT_FAIL("profile_build", rc);
    }
    rc = bm_mp_boot_cpu_attach_and_init();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: attach rc=%d", rc);
        BOOT_FAIL("cpu_attach", rc);
    }
    rc = bm_mp_barrier_wait(BM_MP_BOOT_RUNTIME_READY,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: barrier runtime rc=%d", rc);
        BOOT_FAIL("barrier_runtime", rc);
    }
    rc = cpu0_init_hook();
    if (rc != BM_OK) {
        bm_mp_boot_report_failure();
        BM_LOGE(TAG, "boot fail: cpu0 init rc=%d", rc);
        BOOT_FAIL("cpu0_init", rc);
    }
    rc = bm_mp_barrier_wait(BM_MP_BOOT_INIT_READY,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: barrier init rc=%d", rc);
        BOOT_FAIL("barrier_init", rc);
    }
    rc = bm_mp_barrier_wait(BM_MP_BOOT_START_READY,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: barrier start rc=%d", rc);
        BOOT_FAIL("barrier_start", rc);
    }
    bm_mp_boot_release_irq();
    rc = bm_mp_barrier_wait(BM_MP_BOOT_IRQ_RELEASE,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: barrier irq rc=%d", rc);
        BOOT_FAIL("barrier_irq", rc);
    }
    rc = bm_exec_irq_release_on_this_cpu();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "boot fail: irq_release rc=%d", rc);
        BOOT_FAIL("irq_release", rc);
    }
    (void)bm_wdg_register("dual_stream");
    return BM_OK;
}

int main(void) {
    uint32_t loops = 0u;
    uint32_t drain;
    bm_mp_ipc_matrix_t *matrix;

    BM_LOGI(TAG, "dual_core_stream start");
    bm_hal_uart_init(NULL);
#ifdef BM_EXAMPLE_QEMU_SMP
    hybrid_print("EXAMPLE_DUAL_CORE_STREAM: start\n");
#endif
    streams_bind_owners();
    (void)bm_module_boot();
    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    {
        int boot_rc = bootstrap_mp();

        if (boot_rc != BM_OK) {
            hybrid_print("EXAMPLE_DUAL_CORE_STREAM: FAIL boot rc=");
            example_print_u32((uint32_t)(-boot_rc));
            hybrid_print("\n");
            return 1;
        }
#ifdef BM_EXAMPLE_QEMU_SMP
        hybrid_print("EXAMPLE_DUAL_CORE_STREAM: boot ok\n");
#endif
    }

    g_axis0.enabled = 1;
    g_axis1.enabled = 1;
    bm_atomic_ipc_store_u32(&g_relay_done_ipc, 0u);
    bm_atomic_ipc_store_u32(&g_axis1_produced_ipc, 0u);
    bm_atomic_ipc_store_u32(&g_axis1_count_ipc, 0u);

    while (loops < DEMO_LOOPS ||
           g_axis0.processed < PASS_BLOCKS_PER_CPU ||
           bm_atomic_ipc_load_u32(&g_relay_done_ipc) == 0u ||
           bm_atomic_ipc_load_u32(&g_axis1_count_ipc) <
               PASS_BLOCKS_PER_CPU) {
#ifdef BM_EXAMPLE_QEMU_SMP
        bm_mp_cpu_main_iteration();
        bm_wdg_feed_module("dual_stream");
        (void)bm_wdg_feed();
        /*
         * QEMU 的两个 hart 由同一宿主线程时间片调度。每轮进入 WFI，等待本核
         * 下一次 CLINT tick，既保持主循环周期语义，也保证从核获得执行机会；
         * 不使用固定延时或伪造计数。
         */
        bm_mp_idle_until_interrupt();
        loops++;
#else
        uint32_t start_ticks;
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#endif
        start_ticks = bm_hal_timer_get_ticks();
        bm_mp_cpu_main_iteration();
        bm_mp_enforce_main_loop_period(start_ticks);
        bm_wdg_feed_module("dual_stream");
        (void)bm_wdg_feed();
        bm_mp_idle_until_interrupt();
        loops++;
#endif
        if (loops > (DEMO_LOOPS + 2000u)) {
            break;
        }
    }

    bm_mp_signal_demo_stop();
    for (drain = 0u; drain < 500u; drain++) {
        bm_mp_idle_until_interrupt();
    }
    (void)bm_mp_join_secondary_cpus();

    matrix = bm_mp_ipc_matrix();
    (void)matrix;
    {
        const bm_stream_relay_stats_t *relay_stats =
            bm_stream_relay_stats(&g_relay);

        if (relay_stats != NULL &&
            relay_stats->delivered >= PASS_BLOCKS_PER_CPU) {
            bm_atomic_ipc_store_u32(&g_relay_done_ipc, 1u);
        }
    }
    {
        uint32_t relay_ok = bm_atomic_ipc_load_u32(&g_relay_done_ipc);
        uint32_t axis1_produced = bm_atomic_ipc_load_u32(&g_axis1_produced_ipc);
        uint32_t axis1_processed = bm_atomic_ipc_load_u32(&g_axis1_count_ipc);

        (void)axis1_produced;
        BM_LOGI(TAG,
                "cpu0 p=%u c=%u cpu1 p=%u c=%u relay=%u matrix=[%u,%u]",
                (unsigned)g_axis0.produced, (unsigned)g_axis0.processed,
                (unsigned)axis1_produced, (unsigned)axis1_processed,
                (unsigned)relay_ok,
                (unsigned)(matrix ? bm_mp_ipc_stream_blocks_processed(0u) : 0u),
                (unsigned)(matrix ? bm_mp_ipc_stream_blocks_processed(1u) : 0u));

        if (g_axis0.processed >= PASS_BLOCKS_PER_CPU &&
            axis1_processed >= PASS_BLOCKS_PER_CPU && relay_ok != 0u) {
            hybrid_print_pass("DUAL_CORE_STREAM");
#ifdef BM_EXAMPLE_QEMU_SMP
            qemu_test_finish(1);
#endif
            return 0;
        }
    }
    {
        const bm_stream_relay_stats_t *relay_stats =
            bm_stream_relay_stats(&g_relay);

        if (relay_stats != NULL) {
            BM_LOGI(TAG, "relay delivered=%u drop=%u",
                    (unsigned)relay_stats->delivered,
                    (unsigned)relay_stats->drop);
        }
    }
    hybrid_print("EXAMPLE_DUAL_CORE_STREAM: FAIL tracking\n");
#ifdef BM_EXAMPLE_QEMU_SMP
    hybrid_print("  cpu0 p=");
    example_print_u32(g_axis0.produced);
    hybrid_print(" c=");
    example_print_u32(g_axis0.processed);
    hybrid_print(" cpu1 p=");
    example_print_u32(bm_atomic_ipc_load_u32(&g_axis1_produced_ipc));
    hybrid_print(" c=");
    example_print_u32(bm_atomic_ipc_load_u32(&g_axis1_count_ipc));
    hybrid_print(" relay=");
    example_print_u32(bm_atomic_ipc_load_u32(&g_relay_done_ipc));
    hybrid_print("\n");
#endif
#ifdef BM_EXAMPLE_QEMU_SMP
    qemu_test_finish(0);
#endif
    return 1;
}

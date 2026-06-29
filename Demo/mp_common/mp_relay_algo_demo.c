/**
 * @file mp_relay_algo_demo.c
 * @brief 双核 relay + bmp_algo 工业算法 Demo 实现
 *
 * CPU0：bm_stream 产块并经 relay 转发；CPU1：relay 收块后调用 bmp_* API。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-16
 *
 * @par 修改日志:
 *
 * Date       Version Author Description
 * 2026-06-16 1.0     zeh    首版
 *
 */
#include "mp_relay_algo_demo.h"

#include "bmp/algo/bmp_algo_audio.h"
#include "bmp/algo/bmp_algo_bms.h"
#include "bmp/algo/bmp_algo_fft.h"
#include "bmp/algo/bmp_algo_motor.h"
#include "bmp/algo/bmp_algo_vibration.h"

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
#include <math.h>
#include <stdint.h>
#include <string.h>

const bm_module_t *_bm_module_table[1] = { NULL };
const uint32_t _bm_module_count = 0u;

#define MP_RELAY_MAX_SAMPLES   64u
#define MP_RELAY_SLOT_PERIOD_US 4000u
#define TWO_PI                 6.283185307f
#define DEMO_LOOPS             8000u

#ifdef BM_EXAMPLE_QEMU_SMP
/*
 * QEMU 主循环周期为 10ms；块 deadline 须与之对齐，
 * 否则 hard RT safe-stop 在首次迟到块即停机。
 */
#undef MP_RELAY_SLOT_PERIOD_US
#define MP_RELAY_SLOT_PERIOD_US        10000u
#undef DEMO_LOOPS
#define DEMO_LOOPS                     1200u
#define MP_RELAY_BLOCK_DEADLINE_US     50000u
#else
#define MP_RELAY_BLOCK_DEADLINE_US     MP_RELAY_SLOT_PERIOD_US
#endif

typedef struct {
    float samples[MP_RELAY_MAX_SAMPLES];
    uint32_t count;
} mp_relay_pcm_t;

typedef struct {
    float    phase;
    uint32_t produced;
    uint32_t processed;
    float    bms_voltage;
    float    motor_speed;
    int      enabled;
} axis_state_t;

BM_STREAM_PAYLOADS(g_stream0, mp_relay_pcm_t, 4u);
BM_STREAM_BLOCKS(g_stream0, 4u);
BM_STREAM_INSTANCE(g_stream0, 4u);

BM_STREAM_RELAY_SLOTS(g_relay_slots, 4u, sizeof(mp_relay_pcm_t));
static BM_CACHE_ALIGNAS(BM_CONFIG_CACHE_LINE) bm_stream_relay_t g_relay = {
    .source_cpu = 0u,
    .target_cpu = 1u,
    .slot_bytes = (uint32_t)sizeof(mp_relay_pcm_t),
    .depth = 4u,
    .slots = _bm_stream_relay_slots_g_relay_slots,
    .cache_policy = BM_STREAM_RELAY_CACHE_NON_CACHEABLE
};

static axis_state_t g_axis0;
static axis_state_t g_axis1;
static bm_atomic_ipc_u32_t g_pass_done_ipc = BM_ATOMIC_IPC_U32_INIT(0u);
static bm_atomic_ipc_u32_t g_step_count_ipc = BM_ATOMIC_IPC_U32_INIT(0u);

static bmp_fft_state_t g_fft;
static bmp_vib_state_t g_vib;
static bmp_bms_state_t g_bms;
static bmp_motor_state_t g_motor;
static bmp_audio_state_t g_audio;

static void fill_block_timestamp(bm_timestamp_t *ts) {
    bm_hal_timer_handle_t timer = bm_hal_timer_for_cpu(bm_hal_cpu_id());

    ts->clock_id = timer.clock_id;
    ts->clock_epoch = timer.clock_epoch;
    ts->quality = 1u;
    ts->rate_hz = bm_hal_timer_get_freq();
    ts->ticks = bm_hal_timer_get_ticks();
}

static void fill_pcm_sine(axis_state_t *axis, mp_relay_pcm_t *pcm) {
    uint32_t n = g_mp_relay_algo_params.samples_per_block;
    float step;
    uint32_t i;

    if (n > MP_RELAY_MAX_SAMPLES) {
        n = MP_RELAY_MAX_SAMPLES;
    }
    step = TWO_PI * g_mp_relay_algo_params.signal_freq_hz /
           g_mp_relay_algo_params.sample_rate_hz;
    for (i = 0u; i < n; i++) {
        pcm->samples[i] = 0.5f * sinf(axis->phase);
        axis->phase += step;
        if (axis->phase >= TWO_PI) {
            axis->phase -= TWO_PI;
        }
    }
    pcm->count = n;
}

static void fill_bms_block(axis_state_t *axis, mp_relay_pcm_t *pcm) {
    pcm->samples[0] = -0.5f;
    pcm->samples[1] = axis->bms_voltage;
    pcm->count = 2u;
    axis->bms_voltage -= 0.002f;
    if (axis->bms_voltage < 3.2f) {
        axis->bms_voltage = 4.0f;
    }
}

static void fill_motor_block(axis_state_t *axis, mp_relay_pcm_t *pcm) {
    float target = g_mp_relay_algo_params.signal_freq_hz;
    float noise = 0.1f * sinf(axis->phase);

    pcm->samples[0] = target + noise;
    pcm->count = 1u;
    axis->phase += 0.3f;
}

static void fill_block(axis_state_t *axis, mp_relay_pcm_t *pcm) {
    switch (g_mp_relay_algo_params.kind) {
    case MP_RELAY_ALGO_BMS:
        fill_bms_block(axis, pcm);
        break;
    case MP_RELAY_ALGO_MOTOR:
        fill_motor_block(axis, pcm);
        break;
    default:
        fill_pcm_sine(axis, pcm);
        break;
    }
}

static int algo_init_on_cpu1(void) {
    switch (g_mp_relay_algo_params.kind) {
    case MP_RELAY_ALGO_FFT: {
        bmp_fft_config_t cfg = {
            g_mp_relay_algo_params.samples_per_block,
            g_mp_relay_algo_params.sample_rate_hz
        };
        return bmp_fft_enhanced_init(&g_fft, &cfg);
    }
    case MP_RELAY_ALGO_VIBRATION: {
        bmp_vib_config_t cfg = {
            g_mp_relay_algo_params.sample_rate_hz,
            g_mp_relay_algo_params.signal_freq_hz,
            g_mp_relay_algo_params.samples_per_block
        };
        return bmp_vib_diag_init(&g_vib, &cfg);
    }
    case MP_RELAY_ALGO_BMS: {
        bmp_bms_config_t cfg = { 10.0f, 0.2f };
        return bmp_bms_fusion_init(&g_bms, &cfg, 0.8f);
    }
    case MP_RELAY_ALGO_MOTOR: {
        bmp_motor_config_t cfg = { 0.01f, 0.5f };
        return bmp_motor_observer_init(&g_motor, &cfg,
                                       g_mp_relay_algo_params.signal_freq_hz);
    }
    case MP_RELAY_ALGO_AUDIO: {
        bmp_audio_config_t cfg = {
            0.5f, g_mp_relay_algo_params.sample_rate_hz
        };
        return bmp_audio_agc_init(&g_audio, &cfg);
    }
    default:
        return -1;
    }
}

static int algo_check_pass(const mp_relay_pcm_t *pcm) {
    uint32_t pass_n = g_mp_relay_algo_params.pass_blocks;

    switch (g_mp_relay_algo_params.kind) {
    case MP_RELAY_ALGO_FFT: {
        bmp_fft_result_t res;
        if (bmp_fft_enhanced_step(&g_fft, pcm->samples, pcm->count, &res) != 0) {
            return 0;
        }
        if (bm_atomic_ipc_load_u32(&g_step_count_ipc) >= pass_n &&
            res.peak_mag > 0.05f) {
            uint32_t expect = (uint32_t)(
                g_mp_relay_algo_params.signal_freq_hz *
                (float)g_mp_relay_algo_params.samples_per_block /
                g_mp_relay_algo_params.sample_rate_hz);
            if (expect < 1u) {
                expect = 1u;
            }
            if (res.peak_bin == expect || res.peak_bin == expect + 1u ||
                res.peak_bin == expect - 1u) {
                return 1;
            }
        }
        break;
    }
    case MP_RELAY_ALGO_VIBRATION: {
        bmp_vib_result_t res;
        if (bmp_vib_diag_step(&g_vib, pcm->samples, pcm->count, &res) != 0) {
            return 0;
        }
        if (bm_atomic_ipc_load_u32(&g_step_count_ipc) >= pass_n &&
            res.rms > 0.1f && res.fault_score > 0.3f) {
            return 1;
        }
        break;
    }
    case MP_RELAY_ALGO_BMS: {
        bmp_bms_config_t cfg = { 10.0f, 0.2f };
        float soc;
        float dt = (float)MP_RELAY_SLOT_PERIOD_US / 1000000.0f;
        if (bmp_bms_fusion_step(&g_bms, &cfg, pcm->samples[0], pcm->samples[1],
                                dt, &soc) != 0) {
            return 0;
        }
        if (bm_atomic_ipc_load_u32(&g_step_count_ipc) >= pass_n &&
            soc > 0.1f && soc < 1.0f) {
            return 1;
        }
        break;
    }
    case MP_RELAY_ALGO_MOTOR: {
        bmp_motor_config_t cfg = { 0.01f, 0.5f };
        float est;
        float dt = (float)MP_RELAY_SLOT_PERIOD_US / 1000000.0f;
        if (bmp_motor_observer_step(&g_motor, &cfg, pcm->samples[0], dt, &est) != 0) {
            return 0;
        }
        if (bm_atomic_ipc_load_u32(&g_step_count_ipc) >= pass_n &&
            fabsf(est - g_mp_relay_algo_params.signal_freq_hz) < 0.5f) {
            return 1;
        }
        break;
    }
    case MP_RELAY_ALGO_AUDIO: {
        bmp_audio_config_t cfg = {
            0.5f, g_mp_relay_algo_params.sample_rate_hz
        };
        float out[MP_RELAY_MAX_SAMPLES];
        uint32_t i;
        float peak = 0.0f;
        if (bmp_audio_agc_step(&g_audio, &cfg, pcm->samples, out, pcm->count) != 0) {
            return 0;
        }
        for (i = 0u; i < pcm->count; i++) {
            float a = fabsf(out[i]);
            if (a > peak) {
                peak = a;
            }
        }
        if (bm_atomic_ipc_load_u32(&g_step_count_ipc) >= pass_n &&
            peak > 0.2f) {
            return 1;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

static void cpu0_producer(const bm_exec_t *inst) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    mp_relay_pcm_t *pcm;
    bm_block_t *block;
    bm_timestamp_t ts;

    if (!axis || !axis->enabled) {
        return;
    }
    if (bm_stream_producer_acquire(&g_stream0, &block) != BM_OK) {
        return;
    }
    pcm = (mp_relay_pcm_t *)block->data;
    fill_block(axis, pcm);
    fill_block_timestamp(&ts);
    if (bm_stream_producer_commit(&g_stream0, block,
                                  (uint32_t)sizeof(mp_relay_pcm_t), &ts) == BM_OK) {
        axis->produced++;
    }
}

static void cpu0_consumer(const bm_exec_t *inst, bm_block_t *block) {
    axis_state_t *axis = (axis_state_t *)inst->state;
    const mp_relay_pcm_t *pcm;

    axis->processed++;
    pcm = (const mp_relay_pcm_t *)block->data;
  (void)bm_stream_relay_publish(&g_relay, pcm, sizeof(mp_relay_pcm_t));
}

static void cpu1_idle(const bm_exec_t *inst) {
    (void)inst;
}

static void relay_consume(bm_stream_relay_t *relay, const void *payload,
                          uint32_t len, uint32_t sequence, void *context) {
    const mp_relay_pcm_t *pcm;
    (void)relay;
    (void)len;
    (void)sequence;
    (void)context;

    pcm = (const mp_relay_pcm_t *)payload;
    bm_atomic_ipc_inc_u32(&g_step_count_ipc);
    if (algo_check_pass(pcm)) {
        bm_atomic_ipc_store_u32(&g_pass_done_ipc, 1u);
    }
}

static const bm_exec_slot_t g_cpu0_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = MP_RELAY_SLOT_PERIOD_US,
        .run = cpu0_producer,
        .name = "cpu0_prod"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = MP_RELAY_BLOCK_DEADLINE_US,
        .flags = BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE,
        .run_block = cpu0_consumer,
        .stream = &g_stream0,
        .name = "cpu0_cons"
    }
};

static const bm_exec_slot_t g_cpu1_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = MP_RELAY_SLOT_PERIOD_US,
        .run = cpu1_idle,
        .name = "cpu1_idle"
    }
};

static int axis_init(const bm_exec_t *inst) {
    axis_state_t *axis = (axis_state_t *)inst->state;

    if (!axis) {
        return BM_ERR_INVALID;
    }
    memset(axis, 0, sizeof(*axis));
    axis->enabled = 1;
    axis->bms_voltage = 4.0f;
    if (inst->owner_cpu == 0u) {
        g_stream0.owner_cpu = 0u;
        return bm_stream_init(&g_stream0, _bm_stream_payload_g_stream0, 4u,
                              sizeof(mp_relay_pcm_t));
    }
    return BM_OK;
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
    1u, 0u, "cpu0_algo", &g_axis0, NULL, NULL,
    g_cpu0_slots, 2u, NULL, 0u, &g_axis_ops
};

static const bm_exec_t g_exec_cpu1 = {
    2u, 1u, "cpu1_algo", &g_axis1, NULL, NULL,
    g_cpu1_slots, 1u, NULL, 0u, &g_axis_ops
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
        return rc;
    }
    rc = bm_exec_init_on_this_cpu(g_instances, 2u);
    if (rc != BM_OK) {
        return rc;
    }
    return bm_exec_prepare_on_this_cpu(g_instances, 2u);
}

static int cpu1_init_hook(void) {
    int rc;
    uint32_t tries;

    rc = bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);
    if (rc != BM_OK) {
        return rc;
    }
    rc = algo_init_on_cpu1();
    if (rc != 0) {
        return BM_ERR_INVALID;
    }
    bm_mp_profile_bind_epoch_on_this_cpu();
    rc = bm_exec_init_on_this_cpu(g_instances, 2u);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_exec_prepare_on_this_cpu(g_instances, 2u);
    if (rc != BM_OK) {
        return rc;
    }
    for (tries = 0u; tries < 1000000u; tries++) {
        rc = bm_stream_relay_register_on_this_cpu(&g_relay, relay_consume, NULL);
        if (rc == BM_OK) {
            return BM_OK;
        }
        if (rc != BM_ERR_NOT_INIT) {
            return rc;
        }
        bm_hal_cpu_yield();
    }
    return BM_ERR_TIMEOUT;
}

static void cpu1_iter_hook(void) {
#ifdef NATIVE_SIM
    bm_hal_timer_native_advance_ticks(1u);
#endif
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

/**
 * @brief QEMU 协作定时：在 exec deadline 检查前推进 tick 并 drain Block/relay
 */
void bm_mp_coop_pre_iteration(uint32_t cpu) {
    qemu_rv64_smp_on_timer_irq();
    if (cpu == 0u) {
        qemu_exec_drain_blocks(&g_exec_cpu0, g_cpu0_slots,
                               (uint32_t)(sizeof(g_cpu0_slots) /
                                          sizeof(g_cpu0_slots[0])));
    } else if (cpu == 1u) {
        (void)bm_stream_relay_drain_on_this_cpu(
            BM_CONFIG_MP_RELAY_DRAIN_BUDGET);
    }
}
#endif

static void register_schedule(void) {
    static const bm_mp_stream_gate_params_t stream_gate = {
        4u, MP_RELAY_SLOT_PERIOD_US, 120u, 5000u, 200u, 0u, 2u, 5u, 10u
    };
    static const bm_mp_schedule_slot_t slots[] = {
        { "cpu0_prod", 0u, 80u, MP_RELAY_SLOT_PERIOD_US, MP_RELAY_SLOT_PERIOD_US,
          20u, 5u, 0u, 0u, 0u, 0u, 0u },
        { "cpu0_cons", 0u, 120u, MP_RELAY_SLOT_PERIOD_US, 5000u, 30u, 10u,
          BM_MP_SCHEDULE_FLAG_STREAM, 0u, 0u, 0u, 0u },
        { "relay_drain", 1u, 40u, MP_RELAY_SLOT_PERIOD_US, MP_RELAY_SLOT_PERIOD_US,
          10u, 5u, BM_MP_SCHEDULE_FLAG_RELAY, 0u, 0u, 20u, 10u },
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
    BM_LOGE(g_mp_relay_algo_params.tag, "mp watchdog safe-stop");
}

static int bootstrap_mp(void) {
    int rc;

    bm_mp_set_cpu_init_hook(1u, cpu1_init_hook);
    bm_mp_set_cpu_iter_hook(1u, cpu1_iter_hook);
    bm_mp_wdg_set_safe_stop_hook(mp_safe_stop);

    rc = bm_mp_init(BM_CONFIG_CPU_COUNT);
    if (rc != BM_OK) {
        return rc;
    }
    bm_mp_set_demo_max_loops(0u);
    rc = bm_hal_cpu_boot_secondary((uintptr_t)bm_mp_cpu_secondary_entry);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_mp_boot_bootstrap_sequence();
    if (rc != BM_OK) {
        return rc;
    }
    register_schedule();
    bm_mp_profile_register_exec(g_instances, 2u);
    rc = bm_mp_profile_build();
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_mp_boot_cpu_attach_and_init();
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_mp_barrier_wait(BM_MP_BOOT_RUNTIME_READY,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        return rc;
    }
    rc = cpu0_init_hook();
    if (rc != BM_OK) {
        bm_mp_boot_report_failure();
        return rc;
    }
    rc = bm_mp_barrier_wait(BM_MP_BOOT_INIT_READY,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_mp_barrier_wait(BM_MP_BOOT_START_READY,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        return rc;
    }
    bm_mp_boot_release_irq();
    rc = bm_mp_barrier_wait(BM_MP_BOOT_IRQ_RELEASE,
                            BM_CONFIG_MP_BOOT_BARRIER_TIMEOUT_US);
    if (rc != BM_OK) {
        return rc;
    }
    rc = bm_exec_irq_release_on_this_cpu();
    if (rc != BM_OK) {
        return rc;
    }
    (void)bm_wdg_register(g_mp_relay_algo_params.wdg_name);
    return BM_OK;
}

int mp_relay_algo_demo_main(void) {
    uint32_t loops = 0u;
    uint32_t drain;
    const char *tag = g_mp_relay_algo_params.tag;
    (void)tag;

    BM_LOGI(tag, "relay algo demo start kind=%u", (unsigned)g_mp_relay_algo_params.kind);
    bm_hal_uart_init(NULL);
#ifdef BM_EXAMPLE_QEMU_SMP
    hybrid_print("EXAMPLE_RELAY_ALGO: start\n");
#endif
    g_stream0.owner_cpu = 0u;
    (void)bm_module_boot();
    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    if (bootstrap_mp() != BM_OK) {
        hybrid_print("EXAMPLE_RELAY_ALGO: FAIL boot\n");
        return 1;
    }
#ifdef BM_EXAMPLE_QEMU_SMP
    hybrid_print("EXAMPLE_RELAY_ALGO: boot ok\n");
#endif

    g_axis0.enabled = 1;
    g_axis1.enabled = 1;
    bm_atomic_ipc_store_u32(&g_pass_done_ipc, 0u);
    bm_atomic_ipc_store_u32(&g_step_count_ipc, 0u);

    while (loops < DEMO_LOOPS && bm_atomic_ipc_load_u32(&g_pass_done_ipc) == 0u) {
#ifdef BM_EXAMPLE_QEMU_SMP
        bm_mp_cpu_main_iteration();
        bm_wdg_feed_module(g_mp_relay_algo_params.wdg_name);
        (void)bm_wdg_feed();
        loops++;
#else
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#endif
        {
            uint32_t start_ticks = bm_hal_timer_get_ticks();
            bm_mp_cpu_main_iteration();
            bm_mp_enforce_main_loop_period(start_ticks);
        }
        bm_wdg_feed_module(g_mp_relay_algo_params.wdg_name);
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

    {
        const bm_stream_relay_stats_t *relay_stats = bm_stream_relay_stats(&g_relay);
        if (relay_stats != NULL &&
            relay_stats->delivered >= g_mp_relay_algo_params.pass_blocks) {
            if (bm_atomic_ipc_load_u32(&g_step_count_ipc) >=
                g_mp_relay_algo_params.pass_blocks) {
                bm_atomic_ipc_store_u32(&g_pass_done_ipc, 1u);
            }
        }
    }

    BM_LOGI(tag, "cpu0 p=%u c=%u algo_steps=%u ok=%d",
            (unsigned)g_axis0.produced, (unsigned)g_axis0.processed,
            (unsigned)bm_atomic_ipc_load_u32(&g_step_count_ipc),
            (int)bm_atomic_ipc_load_u32(&g_pass_done_ipc));

    if (g_axis0.processed >= g_mp_relay_algo_params.pass_blocks &&
        bm_atomic_ipc_load_u32(&g_pass_done_ipc) != 0u) {
        hybrid_print_pass(g_mp_relay_algo_params.pass_label);
        return 0;
    }
    hybrid_print("EXAMPLE_RELAY_ALGO: FAIL tracking\n");
#ifdef BM_EXAMPLE_QEMU_SMP
    hybrid_print("  produced=");
    example_print_u32(g_axis0.produced);
    hybrid_print(" processed=");
    example_print_u32(g_axis0.processed);
    hybrid_print(" algo_steps=");
    example_print_u32(bm_atomic_ipc_load_u32(&g_step_count_ipc));
    hybrid_print(" ok=");
    example_print_u32(bm_atomic_ipc_load_u32(&g_pass_done_ipc));
    hybrid_print("\n");
#else
    hybrid_print("  produced=");
    example_print_u32(g_axis0.produced);
    hybrid_print(" processed=");
    example_print_u32(g_axis0.processed);
    hybrid_print(" algo_steps=");
    example_print_u32(bm_atomic_ipc_load_u32(&g_step_count_ipc));
    hybrid_print("\n");
#endif
    return 1;
}

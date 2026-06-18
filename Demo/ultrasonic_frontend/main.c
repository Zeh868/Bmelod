/**
 * @file main.c
 * @brief 超声块流示例：合成回波突发 + ToF + 同步检波
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 */
#include "app_ultrasonic.h"
#include "bm/algorithm/bm_algo_detection.h"
#include "bm_event.h"
#include "bm_exec.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_stream.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <math.h>
#include <stdint.h>
#include <string.h>

#define TAG "us_front"

#ifdef BM_EXAMPLE_QEMU
#define US_POLL_MS             20u
#define US_QEMU_SIM_CYCLES     60000u
#else
#define US_POLL_MS             100u
#endif

#define US_SAMPLES_PER_BLOCK   64u
#define US_BLOCK_PERIOD_US     2000u
#define US_BLOCK_BYTES         (US_SAMPLES_PER_BLOCK * sizeof(float))
#define US_BLOCK_DEPTH         4u
#define US_CARRIER_HZ          40000.0f
#define US_SAMPLE_RATE_HZ      1000000u
#define US_TWO_PI              6.283185307f
#define US_BURST_AMP           1.0f
#define US_TOF_MIN_DELAY       4u
#define US_TOF_THRESHOLD       0.25f
#define US_TOF_ENVELOPE_ALPHA  0.5f
#define US_DEMOD_ALPHA         0.5f

typedef struct {
    float    echo[US_SAMPLES_PER_BLOCK];
} us_echo_block_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    int32_t  last_tof;
    float    last_demod_mag;
    uint32_t last_sequence;
    int      enabled;
} us_axis_state_t;

us_supervisor_metrics_t g_us_metrics;

BM_STREAM_PAYLOADS(g_us_payloads, us_echo_block_t, US_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_us_stream, US_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_us_stream, US_BLOCK_DEPTH);

static us_axis_state_t g_us_state;

static void fill_synthetic_echo(us_echo_block_t *payload) {
    uint32_t i;

    for (i = 0u; i < US_SAMPLES_PER_BLOCK; ++i) {
        float env = 0.0f;
        uint32_t dist;

        if (i >= US_EXPECTED_TOF_INDEX) {
            dist = i - US_EXPECTED_TOF_INDEX;
            if (dist < US_BURST_WIDTH) {
                env = US_BURST_AMP *
                      (1.0f - (float)dist / (float)US_BURST_WIDTH);
            }
        }
        payload->echo[i] = env;
    }
}

static void process_echo_block(const us_echo_block_t *payload,
                               int32_t *tof_out,
                               float *demod_mag_out) {
    bm_algo_sync_demod_state_t demod;
    uint32_t i;
    float carrier_step = US_TWO_PI * US_CARRIER_HZ / (float)US_SAMPLE_RATE_HZ;
    float peak_mag = 0.0f;
    int32_t tof;

    if (payload == NULL || tof_out == NULL || demod_mag_out == NULL) {
        return;
    }

    bm_algo_sync_demod_reset(&demod);
    demod.alpha = US_DEMOD_ALPHA;

    for (i = 0u; i < US_SAMPLES_PER_BLOCK; ++i) {
        float env = payload->echo[i];
        float phase = carrier_step * (float)i;
        float sin_ref = sinf(phase);
        float cos_ref = cosf(phase);
        float rf = env * cos_ref;

        bm_algo_sync_demod_feed(&demod, rf, sin_ref, cos_ref);
        {
            float mag = bm_algo_sync_demod_magnitude(&demod);

            if (mag > peak_mag) {
                peak_mag = mag;
            }
        }
    }

    tof = bm_algo_ultrasonic_tof(payload->echo, US_SAMPLES_PER_BLOCK,
                                 US_TOF_MIN_DELAY, US_TOF_THRESHOLD,
                                 US_TOF_ENVELOPE_ALPHA);
    *tof_out = tof;
    *demod_mag_out = peak_mag;
}

static void us_producer_step(const bm_exec_t *instance) {
    us_axis_state_t *state = (us_axis_state_t *)instance->state;
    us_echo_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;

    if (state == NULL || state->enabled == 0) {
        return;
    }

    if (bm_stream_producer_acquire(&g_us_stream, &block) != BM_OK) {
        return;
    }

    payload = (us_echo_block_t *)block->data;
    fill_synthetic_echo(payload);

    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_us_stream, block, US_BLOCK_BYTES,
                                  &timestamp) != BM_OK) {
        BM_LOGW(TAG, "producer commit failed");
        return;
    }

    state->blocks_produced++;
    g_us_metrics.blocks_produced = state->blocks_produced;
}

static void us_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    us_axis_state_t *state = (us_axis_state_t *)instance->state;
    const us_echo_block_t *payload;
    int32_t tof;
    float demod_mag;

    if (state == NULL || block == NULL || block->data == NULL) {
        return;
    }

    payload = (const us_echo_block_t *)block->data;
    process_echo_block(payload, &tof, &demod_mag);

    state->last_tof = tof;
    state->last_demod_mag = demod_mag;
    state->blocks_processed++;
    if (state->last_sequence != 0u &&
        block->sequence != state->last_sequence + 1u) {
        BM_LOGW(TAG, "sequence gap: last=%u got=%u",
                (unsigned)state->last_sequence, (unsigned)block->sequence);
    }
    state->last_sequence = block->sequence;

    g_us_metrics.blocks_processed = state->blocks_processed;
    g_us_metrics.last_tof = tof;
    g_us_metrics.last_demod_mag = demod_mag;

    (void)bm_stream_consumer_release(&g_us_stream, block);
}

static const bm_exec_slot_t g_us_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = US_BLOCK_PERIOD_US,
        .run = us_producer_step,
        .name = "echo_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = US_BLOCK_PERIOD_US,
        .run_block = us_consumer_block,
        .stream = &g_us_stream,
        .name = "us_block"
    }
};

static int us_exec_init(const bm_exec_t *instance) {
    us_axis_state_t *state = (us_axis_state_t *)instance->state;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_us_stream, _bm_stream_payload_g_us_payloads,
                       US_BLOCK_DEPTH, sizeof(us_echo_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_us_stream);
    return BM_OK;
}

static int us_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void app_us_enable_production(void) {
    g_us_state.enabled = 1;
    g_us_metrics.enable_events++;
}

static void us_exec_safe_stop(const bm_exec_t *instance) {
    us_axis_state_t *state = (us_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_us_ops = {
    us_exec_init, us_exec_start, us_exec_safe_stop
};

static const bm_exec_t g_us_exec_bound = {
    1u,
    "us_front",
    &g_us_state,
    NULL,
    NULL,
    g_us_slots,
    2u,
    NULL,
    0u,
    &g_us_ops
};

static const bm_exec_t *const g_instances[] = { &g_us_exec_bound };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { US_POLL_MS, EVENT_US_POLL, 1u, "tel_poll" }
};

static void run_sim(uint32_t cycles) {
    uint32_t i;

    for (i = 0u; i < cycles; ++i) {
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#elif defined(BM_EXAMPLE_QEMU)
        bm_example_qemu_spin();
#else
        for (volatile uint32_t d = 0u; d < 20u; ++d) {
        }
#endif
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
}

static int tof_within_tolerance(int32_t tof) {
    int32_t err;

    if (tof < 0) {
        return 0;
    }
    err = tof - (int32_t)US_EXPECTED_TOF_INDEX;
    if (err < 0) {
        err = -err;
    }
    return (uint32_t)err <= US_TOF_PASS_TOLERANCE;
}

int main(void) {
    int rc;
    const bm_stream_stats_t *stats;
    us_echo_block_t self_test;
    int32_t self_tof;
    float self_mag;

    fill_synthetic_echo(&self_test);
    process_echo_block(&self_test, &self_tof, &self_mag);
    if (self_tof < 0) {
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL tof\n");
        return 1;
    }
    if (self_mag < US_DEMOD_MAG_MIN) {
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL demod\n");
        return 1;
    }

    BM_LOGI(TAG, "ultrasonic_frontend example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL start\n");
        return 1;
    }

    (void)bm_event_process(8u);

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL ticker\n");
        return 1;
    }

#ifdef NATIVE_SIM
    run_sim(12000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(US_QEMU_SIM_CYCLES);
#else
    run_sim(4000u);
#endif

#ifdef BM_EXAMPLE_QEMU
    if (g_us_metrics.enable_events == 0u) {
        (void)bm_event_publish_copy(EVENT_US_ENABLE, 1u, NULL, 0u);
        (void)bm_event_process(8u);
        run_sim(4000u);
    }
    (void)bm_ticker_poll();
    (void)bm_event_process(8u);
#endif

    stats = bm_stream_stats(&g_us_stream);

    if (g_us_metrics.blocks_processed < US_PASS_MIN_BLOCKS ||
        !tof_within_tolerance(g_us_metrics.last_tof) ||
        g_us_metrics.last_demod_mag < US_DEMOD_MAG_MIN ||
        g_us_metrics.enable_events == 0u ||
        g_us_metrics.telemetry_reads == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        BM_LOGE(TAG, "fail: proc=%u tof=%d mag=%.4f en=%u tel=%u",
                (unsigned)g_us_metrics.blocks_processed,
                (int)g_us_metrics.last_tof,
                (double)g_us_metrics.last_demod_mag,
                (unsigned)g_us_metrics.enable_events,
                (unsigned)g_us_metrics.telemetry_reads);
        hybrid_print("EXAMPLE_ULTRASONIC_FRONTEND: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u tof=%d mag=%.4f",
            (unsigned)g_us_metrics.blocks_processed,
            (int)g_us_metrics.last_tof,
            (double)g_us_metrics.last_demod_mag);
    hybrid_print_pass("ULTRASONIC_FRONTEND");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

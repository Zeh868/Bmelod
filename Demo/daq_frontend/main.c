/**
 * @file main.c
 * @brief DAQ 块流示例：块级 RMS + 波峰因数（bm_algo_statistics / bm_algo_power）
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
#include "app_daq.h"
#include "bm/algorithm/bm_algo_power.h"
#include "bm/algorithm/bm_algo_statistics.h"
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

#define TAG "daq_front"

#ifdef BM_EXAMPLE_QEMU
#define DAQ_POLL_MS            20u
#define DAQ_QEMU_SIM_CYCLES    60000u
#else
#define DAQ_POLL_MS            100u
#endif

#define DAQ_SAMPLE_RATE_HZ     32000u
#define DAQ_SAMPLES_PER_BLOCK  32u
#define DAQ_BLOCK_PERIOD_US    1000u
#define DAQ_BLOCK_BYTES        (DAQ_SAMPLES_PER_BLOCK * sizeof(float))
#define DAQ_BLOCK_DEPTH        4u
#define DAQ_SINE_FREQ_HZ       1000.0f
#define DAQ_TWO_PI             6.283185307f

typedef struct {
    float    samples[DAQ_SAMPLES_PER_BLOCK];
} daq_pcm_block_t;

typedef struct {
    float    phase_rad;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_crest;
    uint32_t last_sequence;
    int      enabled;
} daq_axis_state_t;

daq_supervisor_metrics_t g_daq_metrics;

BM_STREAM_PAYLOADS(g_daq_payloads, daq_pcm_block_t, DAQ_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_daq_stream, DAQ_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_daq_stream, DAQ_BLOCK_DEPTH);

static daq_axis_state_t g_daq_state;
static float g_rms_window[DAQ_SAMPLES_PER_BLOCK];

static void compute_block_metrics(const bm_block_t *block,
                                  float *rms_out,
                                  float *crest_out) {
    const daq_pcm_block_t *payload;
    bm_algo_stats_state_t stats;
    bm_algo_rms_state_t rms_st;
    bm_algo_rms_config_t rms_cfg = { .window_samples = DAQ_SAMPLES_PER_BLOCK };
    uint32_t count;
    uint32_t i;
    float rms_stats;
    float rms_power;

    if (block == NULL || block->data == NULL || rms_out == NULL ||
        crest_out == NULL) {
        return;
    }

    payload = (const daq_pcm_block_t *)block->data;
    count = block->valid_bytes / (uint32_t)sizeof(float);
    if (count > DAQ_SAMPLES_PER_BLOCK) {
        count = DAQ_SAMPLES_PER_BLOCK;
    }

    bm_algo_stats_reset(&stats);
    (void)bm_algo_rms_init(&rms_st, &rms_cfg, g_rms_window,
                           DAQ_SAMPLES_PER_BLOCK);
    bm_algo_rms_reset(&rms_st);

    for (i = 0u; i < count; ++i) {
        float s = payload->samples[i];
        bm_algo_stats_push(&stats, s);
        rms_power = bm_algo_rms_step(&rms_st, &rms_cfg, s);
        (void)rms_power;
    }

    rms_stats = bm_algo_stats_rms(&stats);
    rms_power = bm_algo_array_rms(payload->samples, count);
    *rms_out = 0.5f * (rms_stats + rms_power);
    *crest_out = bm_algo_stats_crest_factor(&stats);
}

static void daq_producer_step(const bm_exec_t *instance) {
    daq_axis_state_t *state = (daq_axis_state_t *)instance->state;
    daq_pcm_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;
    uint32_t i;
    float phase_step;

    if (state == NULL || state->enabled == 0) {
        return;
    }

    if (bm_stream_producer_acquire(&g_daq_stream, &block) != BM_OK) {
        return;
    }

    payload = (daq_pcm_block_t *)block->data;
    phase_step = DAQ_TWO_PI * DAQ_SINE_FREQ_HZ / (float)DAQ_SAMPLE_RATE_HZ;
    for (i = 0u; i < DAQ_SAMPLES_PER_BLOCK; ++i) {
        payload->samples[i] =
            DAQ_SINE_AMPLITUDE * sinf(state->phase_rad);
        state->phase_rad += phase_step;
        if (state->phase_rad >= DAQ_TWO_PI) {
            state->phase_rad -= DAQ_TWO_PI;
        }
    }

    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_daq_stream, block, DAQ_BLOCK_BYTES,
                                  &timestamp) != BM_OK) {
        BM_LOGW(TAG, "producer commit failed");
        return;
    }

    state->blocks_produced++;
    g_daq_metrics.blocks_produced = state->blocks_produced;
}

static void daq_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    daq_axis_state_t *state = (daq_axis_state_t *)instance->state;
    float rms;
    float crest;

    if (state == NULL || block == NULL) {
        return;
    }

    compute_block_metrics(block, &rms, &crest);
    state->last_rms = rms;
    state->last_crest = crest;
    state->blocks_processed++;
    if (state->last_sequence != 0u &&
        block->sequence != state->last_sequence + 1u) {
        BM_LOGW(TAG, "sequence gap: last=%u got=%u",
                (unsigned)state->last_sequence, (unsigned)block->sequence);
    }
    state->last_sequence = block->sequence;

    g_daq_metrics.blocks_processed = state->blocks_processed;
    g_daq_metrics.last_rms = rms;
    g_daq_metrics.last_crest = crest;

    (void)bm_stream_consumer_release(&g_daq_stream, block);
}

static const bm_exec_slot_t g_daq_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = DAQ_BLOCK_PERIOD_US,
        .run = daq_producer_step,
        .name = "dma_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = DAQ_BLOCK_PERIOD_US,
        .run_block = daq_consumer_block,
        .stream = &g_daq_stream,
        .name = "daq_block"
    }
};

static int daq_exec_init(const bm_exec_t *instance) {
    daq_axis_state_t *state = (daq_axis_state_t *)instance->state;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_daq_stream, _bm_stream_payload_g_daq_payloads,
                       DAQ_BLOCK_DEPTH, sizeof(daq_pcm_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_daq_stream);
    return BM_OK;
}

static int daq_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void app_daq_enable_production(void) {
    g_daq_state.enabled = 1;
    g_daq_metrics.enable_events++;
}

static void daq_exec_safe_stop(const bm_exec_t *instance) {
    daq_axis_state_t *state = (daq_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_daq_ops = {
    daq_exec_init, daq_exec_start, daq_exec_safe_stop
};

static const bm_exec_t g_daq_exec_bound = {
    1u,
    "daq_front",
    &g_daq_state,
    NULL,
    NULL,
    g_daq_slots,
    2u,
    NULL,
    0u,
    &g_daq_ops
};

static const bm_exec_t *const g_instances[] = { &g_daq_exec_bound };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { DAQ_POLL_MS, EVENT_DAQ_POLL, 1u, "tel_poll" }
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

int main(void) {
    int rc;
    const bm_stream_stats_t *stats;
    float rms_err;
    float crest_err;

    BM_LOGI(TAG, "daq_frontend example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_DAQ_FRONTEND: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_DAQ_FRONTEND: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_DAQ_FRONTEND: FAIL start\n");
        return 1;
    }

    (void)bm_event_process(8u);

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_DAQ_FRONTEND: FAIL ticker\n");
        return 1;
    }

#ifdef NATIVE_SIM
    run_sim(15000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(DAQ_QEMU_SIM_CYCLES);
#else
    run_sim(4000u);
#endif

#ifdef BM_EXAMPLE_QEMU
    if (g_daq_metrics.enable_events == 0u) {
        (void)bm_event_publish_copy(EVENT_DAQ_ENABLE, 1u, NULL, 0u);
        (void)bm_event_process(8u);
        run_sim(4000u);
    }
    (void)bm_ticker_poll();
    (void)bm_event_process(8u);
#endif

    stats = bm_stream_stats(&g_daq_stream);
    rms_err = fabsf(g_daq_metrics.last_rms - DAQ_EXPECTED_RMS);
    crest_err = fabsf(g_daq_metrics.last_crest - DAQ_EXPECTED_CREST);

    if (g_daq_metrics.blocks_processed < DAQ_PASS_MIN_BLOCKS ||
        rms_err > DAQ_RMS_PASS_TOLERANCE ||
        crest_err > DAQ_CREST_PASS_TOLERANCE ||
        g_daq_metrics.enable_events == 0u ||
        g_daq_metrics.telemetry_reads == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        BM_LOGE(TAG, "fail: proc=%u rms=%.4f crest=%.4f en=%u tel=%u",
                (unsigned)g_daq_metrics.blocks_processed,
                (double)g_daq_metrics.last_rms,
                (double)g_daq_metrics.last_crest,
                (unsigned)g_daq_metrics.enable_events,
                (unsigned)g_daq_metrics.telemetry_reads);
        hybrid_print("EXAMPLE_DAQ_FRONTEND: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u rms=%.4f crest=%.4f",
            (unsigned)g_daq_metrics.blocks_processed,
            (double)g_daq_metrics.last_rms,
            (double)g_daq_metrics.last_crest);
    hybrid_print_pass("DAQ_FRONTEND");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

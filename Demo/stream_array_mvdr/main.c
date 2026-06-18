/**
 * @file main.c
 * @brief 阵列 MVDR 块流示例：多通道正弦 → MVDR → LPF → RMS 遥测
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            初始发布
 */
#include "app_array_mvdr.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/component/audio_array_frontend.h"
#include "bm_event.h"
#include "bm_exec.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_pipeline.h"
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

#define TAG "array_mvdr"
#define ARRAY_TWO_PI  6.283185307f

#ifdef BM_EXAMPLE_QEMU
#define ARRAY_POLL_MS           20u
#define ARRAY_QEMU_SIM_CYCLES   60000u
#else
#define ARRAY_POLL_MS           100u
#endif

typedef struct {
    bm_algo_lpf1_config_t cfg;
    bm_algo_lpf1_state_t  st;
} array_lpf_node_state_t;

typedef struct {
    float    phase_rad;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_energy;
    int      enabled;
} array_mvdr_axis_state_t;

array_mvdr_supervisor_metrics_t g_array_mvdr_metrics;

BM_STREAM_PAYLOADS(g_array_raw_payloads, array_mvdr_pcm_block_t,
                   ARRAY_MVDR_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_array_raw_stream, ARRAY_MVDR_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_array_raw_stream, ARRAY_MVDR_BLOCK_DEPTH);

BM_STREAM_PAYLOADS(g_array_mono_payloads, array_mvdr_mono_block_t,
                   ARRAY_MVDR_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_array_mono_stream, ARRAY_MVDR_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_array_mono_stream, ARRAY_MVDR_BLOCK_DEPTH);

static array_mvdr_axis_state_t g_array_state;
static array_lpf_node_state_t g_lpf_node_state;
static bm_pipeline_t g_array_pipeline;
static bm_audio_array_frontend_axis_t g_array_frontend;
static float g_beam_buffer[ARRAY_MVDR_SAMPLES_PER_BLOCK];

static int array_lpf_prepare(void *state, const void *config) {
    array_lpf_node_state_t *st = (array_lpf_node_state_t *)state;
    const bm_algo_lpf1_config_t *cfg = (const bm_algo_lpf1_config_t *)config;

    if (st == NULL || cfg == NULL) {
        return BM_ERR_INVALID;
    }
    st->cfg = *cfg;
    bm_algo_lpf1_reset(&st->st, 0.0f);
    return BM_OK;
}

static void array_lpf_reset(void *state) {
    array_lpf_node_state_t *st = (array_lpf_node_state_t *)state;

    if (st != NULL) {
        bm_algo_lpf1_reset(&st->st, 0.0f);
    }
}

static int array_lpf_process(void *state,
                             const bm_block_t *input,
                             bm_block_t *output) {
    array_lpf_node_state_t *st = (array_lpf_node_state_t *)state;
    const array_mvdr_mono_block_t *in_payload;
    array_mvdr_mono_block_t *out_payload;
    uint32_t i;
    float sum_sq = 0.0f;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const array_mvdr_mono_block_t *)input->data;
    out_payload = (array_mvdr_mono_block_t *)output->data;
    for (i = 0u; i < ARRAY_MVDR_SAMPLES_PER_BLOCK; ++i) {
        float s = bm_algo_lpf1_step(&st->st, &st->cfg,
                                    in_payload->samples[i]);
        out_payload->samples[i] = s;
        sum_sq += s * s;
    }
    out_payload->block_energy = in_payload->block_energy;
    out_payload->block_rms =
        sqrtf(sum_sq / (float)ARRAY_MVDR_SAMPLES_PER_BLOCK);
    return BM_OK;
}

static const bm_pipeline_node_ops_t g_array_lpf_ops = {
    array_lpf_prepare, array_lpf_process, array_lpf_reset, "lpf1"
};

static const bm_algo_lpf1_config_t g_lpf_cfg = {
    .alpha = 0.2f
};

static bm_pipeline_node_t g_array_nodes[] = {
    {
        .ops = &g_array_lpf_ops,
        .state = &g_lpf_node_state,
        .config = &g_lpf_cfg,
        .input_format = ARRAY_MVDR_FMT_MVDR,
        .output_format = ARRAY_MVDR_FMT_LPF,
        .bypass = 0u
    }
};

static void fill_multichannel_block(array_mvdr_axis_state_t *state,
                                    array_mvdr_pcm_block_t *payload) {
    uint32_t i;
    float step = ARRAY_TWO_PI * ARRAY_MVDR_SINE_FREQ_HZ /
                 (float)ARRAY_MVDR_SAMPLE_RATE_HZ;

    for (i = 0u; i < ARRAY_MVDR_SAMPLES_PER_BLOCK; ++i) {
        float s = ARRAY_MVDR_SINE_AMPLITUDE * sinf(state->phase_rad);
        payload->ch0[i] = s;
        payload->ch1[i] = (i >= (uint32_t)ARRAY_MVDR_CH1_DELAY_SAMPLES)
                              ? ARRAY_MVDR_SINE_AMPLITUDE *
                                    sinf(state->phase_rad -
                                         step * (float)ARRAY_MVDR_CH1_DELAY_SAMPLES)
                              : 0.0f;
        state->phase_rad += step;
        if (state->phase_rad >= ARRAY_TWO_PI) {
            state->phase_rad -= ARRAY_TWO_PI;
        }
    }
}

static void array_producer_step(const bm_exec_t *instance) {
    array_mvdr_axis_state_t *state = (array_mvdr_axis_state_t *)instance->state;
    array_mvdr_pcm_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;

    if (state == NULL || state->enabled == 0) {
        return;
    }
    if (bm_stream_producer_acquire(&g_array_raw_stream, &block) != BM_OK) {
        return;
    }

    payload = (array_mvdr_pcm_block_t *)block->data;
    fill_multichannel_block(state, payload);
    block->format = ARRAY_MVDR_FMT_RAW;
    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_array_raw_stream, block,
                                  (uint32_t)sizeof(array_mvdr_pcm_block_t),
                                  &timestamp) != BM_OK) {
        return;
    }
    state->blocks_produced++;
    g_array_mvdr_metrics.blocks_produced = state->blocks_produced;
}

static void array_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    array_mvdr_axis_state_t *state = (array_mvdr_axis_state_t *)instance->state;
    const array_mvdr_pcm_block_t *raw;
    const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS];
    array_mvdr_mono_block_t mono;
    bm_block_t mono_block;
    uint32_t i;
    float sum_sq = 0.0f;

    if (state == NULL || block == NULL || block->data == NULL) {
        return;
    }

    raw = (const array_mvdr_pcm_block_t *)block->data;
    channels[0] = raw->ch0;
    channels[1] = raw->ch1;

    if (bm_audio_array_frontend_step(&g_array_frontend, channels,
                                     mono.samples,
                                     ARRAY_MVDR_SAMPLES_PER_BLOCK) != BM_OK) {
        (void)bm_stream_consumer_release(&g_array_raw_stream, block);
        return;
    }

    for (i = 0u; i < ARRAY_MVDR_SAMPLES_PER_BLOCK; ++i) {
        sum_sq += mono.samples[i] * mono.samples[i];
    }
    mono.block_energy = g_array_frontend.state.telemetry.energy;
    mono.block_rms = sqrtf(sum_sq / (float)ARRAY_MVDR_SAMPLES_PER_BLOCK);

    memset(&mono_block, 0, sizeof(mono_block));
    mono_block.data = &mono;
    mono_block.valid_bytes = (uint32_t)sizeof(mono);
    mono_block.format = ARRAY_MVDR_FMT_MVDR;

    if (bm_pipeline_process_inplace(&g_array_pipeline, &mono_block) != BM_OK) {
        (void)bm_stream_consumer_release(&g_array_raw_stream, block);
        return;
    }

    state->last_rms = mono.block_rms;
    state->last_energy = mono.block_energy;
    state->blocks_processed++;
    g_array_mvdr_metrics.blocks_processed = state->blocks_processed;
    g_array_mvdr_metrics.last_rms = mono.block_rms;
    g_array_mvdr_metrics.last_energy = mono.block_energy;
    (void)bm_stream_consumer_release(&g_array_raw_stream, block);
}

static const bm_exec_slot_t g_array_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = ARRAY_MVDR_BLOCK_PERIOD_US,
        .run = array_producer_step,
        .name = "array_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = ARRAY_MVDR_BLOCK_PERIOD_US,
        .run_block = array_consumer_block,
        .stream = &g_array_raw_stream,
        .name = "mvdr_pipeline"
    }
};

static int array_exec_init(const bm_exec_t *instance) {
    array_mvdr_axis_state_t *state = (array_mvdr_axis_state_t *)instance->state;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));

    if (bm_stream_init(&g_array_raw_stream,
                       _bm_stream_payload_g_array_raw_payloads,
                       ARRAY_MVDR_BLOCK_DEPTH,
                       sizeof(array_mvdr_pcm_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_array_raw_stream);

    g_array_frontend.config.num_channels = ARRAY_MVDR_NUM_CHANNELS;
    g_array_frontend.config.block_samples = ARRAY_MVDR_SAMPLES_PER_BLOCK;
    g_array_frontend.config.sample_hz = (float)ARRAY_MVDR_SAMPLE_RATE_HZ;
    g_array_frontend.config.use_fixed_delay = 1;
    g_array_frontend.config.fixed_delay_samples[0] = 0;
    g_array_frontend.config.fixed_delay_samples[1] = ARRAY_MVDR_CH1_DELAY_SAMPLES;
    g_array_frontend.config.beam_mode = BM_AUDIO_BEAM_MVDR;
    g_array_frontend.config.mvdr_diagonal_load = 1e-3f;

    if (bm_audio_array_frontend_init(&g_array_frontend, g_beam_buffer,
                                     ARRAY_MVDR_SAMPLES_PER_BLOCK,
                                     NULL, 0u) != BM_OK) {
        return BM_ERR_INVALID;
    }

    rc = bm_pipeline_init(&g_array_pipeline, g_array_nodes,
                          (uint32_t)(sizeof(g_array_nodes) /
                                     sizeof(g_array_nodes[0])));
    return rc;
}

static int array_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void array_exec_safe_stop(const bm_exec_t *instance) {
    array_mvdr_axis_state_t *state = (array_mvdr_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_array_ops = {
    array_exec_init, array_exec_start, array_exec_safe_stop
};

static const bm_exec_t g_array_exec = {
    .id = 1u,
    .owner_cpu = 0u,
    .name = "array_mvdr",
    .state = &g_array_state,
    .config = NULL,
    .resources = NULL,
    .slots = g_array_slots,
    .slot_count = 2u,
    .claims = NULL,
    .claim_count = 0u,
    .ops = &g_array_ops
};

static const bm_exec_t *const g_instances[] = { &g_array_exec };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { ARRAY_POLL_MS, EVENT_ARRAY_MVDR_POLL, 1u, "tel_poll" }
};

void app_array_mvdr_enable_production(void) {
    g_array_state.enabled = 1;
    g_array_mvdr_metrics.enable_events++;
}

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

    BM_LOGI(TAG, "stream_array_mvdr example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_ARRAY_MVDR: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);
    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_ARRAY_MVDR: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_ARRAY_MVDR: FAIL start\n");
        return 1;
    }
    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_ARRAY_MVDR: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(20000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(ARRAY_QEMU_SIM_CYCLES);
#else
    run_sim(5000u);
#endif

    stats = bm_stream_stats(&g_array_raw_stream);
    if (g_array_mvdr_metrics.blocks_processed < ARRAY_MVDR_PASS_MIN_BLOCKS ||
        g_array_mvdr_metrics.last_rms < ARRAY_MVDR_RMS_PASS_MIN ||
        g_array_mvdr_metrics.last_energy < 0.01f ||
        g_array_mvdr_metrics.enable_events == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        hybrid_print("EXAMPLE_STREAM_ARRAY_MVDR: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u rms=%.3f energy=%.3f",
            (unsigned)g_array_mvdr_metrics.blocks_processed,
            (double)g_array_mvdr_metrics.last_rms,
            (double)g_array_mvdr_metrics.last_energy);
    hybrid_print_pass("STREAM_ARRAY_MVDR");
    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

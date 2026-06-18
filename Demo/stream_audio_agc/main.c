/**
 * @file main.c
 * @brief 音频块流示例：低幅正弦 → AGC → 限幅，验证 bm_pipeline + bm_algo_audio
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
#include "app_audio.h"
#include "bm/algorithm/bm_algo_audio.h"
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

#define TAG "audio_agc"
#define AUDIO_TWO_PI  6.283185307f

#ifdef BM_EXAMPLE_QEMU
#define AUDIO_POLL_MS           20u
#define AUDIO_QEMU_SIM_CYCLES   60000u
#else
#define AUDIO_POLL_MS           100u
#endif

typedef struct {
    bm_algo_agc_config_t  cfg;
    bm_algo_agc_state_t   agc;
} audio_agc_node_state_t;

typedef struct {
    bm_algo_limiter_config_t cfg;
} audio_limiter_node_state_t;

typedef struct {
    float    phase_rad;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_peak;
    int      enabled;
} audio_axis_state_t;

audio_supervisor_metrics_t g_audio_metrics;

BM_STREAM_PAYLOADS(g_audio_payloads, audio_pcm_block_t, AUDIO_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_audio_stream, AUDIO_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_audio_stream, AUDIO_BLOCK_DEPTH);

static audio_axis_state_t g_audio_state;
static audio_agc_node_state_t g_agc_node_state;
static audio_limiter_node_state_t g_limiter_node_state;
static bm_pipeline_t g_audio_pipeline;

static int audio_agc_prepare(void *state, const void *config) {
    audio_agc_node_state_t *st = (audio_agc_node_state_t *)state;
    const bm_algo_agc_config_t *cfg = (const bm_algo_agc_config_t *)config;

    if (st == NULL || cfg == NULL) {
        return BM_ERR_INVALID;
    }
    st->cfg = *cfg;
    bm_algo_agc_reset(&st->agc, 1.0f);
    return BM_OK;
}

static void audio_agc_reset(void *state) {
    audio_agc_node_state_t *st = (audio_agc_node_state_t *)state;

    if (st != NULL) {
        bm_algo_agc_reset(&st->agc, 1.0f);
    }
}

static int audio_agc_process(void *state,
                             const bm_block_t *input,
                             bm_block_t *output) {
    audio_agc_node_state_t *st = (audio_agc_node_state_t *)state;
    const audio_pcm_block_t *in_payload;
    audio_pcm_block_t *out_payload;
    uint32_t i;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const audio_pcm_block_t *)input->data;
    out_payload = (audio_pcm_block_t *)output->data;
    bm_algo_agc_process(&st->agc, &st->cfg,
                        in_payload->samples, out_payload->samples,
                        AUDIO_SAMPLES_PER_BLOCK);
    out_payload->peak_level = 0.0f;
    for (i = 0u; i < AUDIO_SAMPLES_PER_BLOCK; ++i) {
        float a = fabsf(out_payload->samples[i]);
        if (a > out_payload->peak_level) {
            out_payload->peak_level = a;
        }
    }
    return BM_OK;
}

static int audio_limiter_prepare(void *state, const void *config) {
    audio_limiter_node_state_t *st = (audio_limiter_node_state_t *)state;
    const bm_algo_limiter_config_t *cfg =
        (const bm_algo_limiter_config_t *)config;

    if (st == NULL || cfg == NULL) {
        return BM_ERR_INVALID;
    }
    st->cfg = *cfg;
    return BM_OK;
}

static void audio_limiter_reset(void *state) {
    (void)state;
}

static int audio_limiter_process(void *state,
                                 const bm_block_t *input,
                                 bm_block_t *output) {
    audio_limiter_node_state_t *st = (audio_limiter_node_state_t *)state;
    const audio_pcm_block_t *in_payload;
    audio_pcm_block_t *out_payload;
    uint32_t i;
    float peak = 0.0f;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const audio_pcm_block_t *)input->data;
    out_payload = (audio_pcm_block_t *)output->data;
    bm_algo_limiter_process(in_payload->samples, out_payload->samples,
                            AUDIO_SAMPLES_PER_BLOCK, &st->cfg);
    for (i = 0u; i < AUDIO_SAMPLES_PER_BLOCK; ++i) {
        float a = fabsf(out_payload->samples[i]);
        if (a > peak) {
            peak = a;
        }
    }
    out_payload->peak_level = peak;
    return BM_OK;
}

static const bm_pipeline_node_ops_t g_audio_agc_ops = {
    audio_agc_prepare, audio_agc_process, audio_agc_reset, "agc"
};

static const bm_pipeline_node_ops_t g_audio_limiter_ops = {
    audio_limiter_prepare, audio_limiter_process, audio_limiter_reset, "limiter"
};

static const bm_algo_agc_config_t g_agc_cfg = {
    .target_level = AUDIO_AGC_TARGET,
    .attack_coeff = 0.2f,
    .release_coeff = 0.05f,
    .gain = 1.0f,
    .min_gain = 0.05f,
    .max_gain = 8.0f,
    .silence_threshold = 0.001f
};

static const bm_algo_limiter_config_t g_limiter_cfg = {
    .threshold = 0.95f,
    .knee = 0.05f
};

static bm_pipeline_node_t g_audio_nodes[] = {
    {
        .ops = &g_audio_agc_ops,
        .state = &g_agc_node_state,
        .config = &g_agc_cfg,
        .input_format = AUDIO_FMT_RAW,
        .output_format = AUDIO_FMT_AGC,
        .bypass = 0u
    },
    {
        .ops = &g_audio_limiter_ops,
        .state = &g_limiter_node_state,
        .config = &g_limiter_cfg,
        .input_format = AUDIO_FMT_AGC,
        .output_format = AUDIO_FMT_LIMITED,
        .bypass = 0u
    }
};

static void fill_sine_block(audio_axis_state_t *state, audio_pcm_block_t *payload) {
    uint32_t i;
    float step = AUDIO_TWO_PI * 1000.0f / (float)AUDIO_SAMPLE_RATE_HZ;

    for (i = 0u; i < AUDIO_SAMPLES_PER_BLOCK; ++i) {
        payload->samples[i] = AUDIO_INPUT_AMPLITUDE * sinf(state->phase_rad);
        state->phase_rad += step;
        if (state->phase_rad >= AUDIO_TWO_PI) {
            state->phase_rad -= AUDIO_TWO_PI;
        }
    }
    payload->peak_level = AUDIO_INPUT_AMPLITUDE;
}

static void audio_producer_step(const bm_exec_t *instance) {
    audio_axis_state_t *state = (audio_axis_state_t *)instance->state;
    audio_pcm_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;

    if (state == NULL || state->enabled == 0) {
        return;
    }
    if (bm_stream_producer_acquire(&g_audio_stream, &block) != BM_OK) {
        return;
    }

    payload = (audio_pcm_block_t *)block->data;
    fill_sine_block(state, payload);
    block->format = AUDIO_FMT_RAW;
    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_audio_stream, block,
                                  (uint32_t)sizeof(audio_pcm_block_t),
                                  &timestamp) != BM_OK) {
        return;
    }
    state->blocks_produced++;
    g_audio_metrics.blocks_produced = state->blocks_produced;
}

static void audio_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    audio_axis_state_t *state = (audio_axis_state_t *)instance->state;
    const audio_pcm_block_t *payload;

    if (state == NULL || block == NULL) {
        return;
    }
    if (bm_pipeline_process_inplace(&g_audio_pipeline, block) != BM_OK) {
        (void)bm_stream_consumer_release(&g_audio_stream, block);
        return;
    }
    payload = (const audio_pcm_block_t *)block->data;
    state->last_peak = payload->peak_level;
    state->blocks_processed++;
    g_audio_metrics.blocks_processed = state->blocks_processed;
    g_audio_metrics.last_peak = payload->peak_level;
    (void)bm_stream_consumer_release(&g_audio_stream, block);
}

static const bm_exec_slot_t g_audio_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = AUDIO_BLOCK_PERIOD_US,
        .run = audio_producer_step,
        .name = "pcm_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = AUDIO_BLOCK_PERIOD_US,
        .run_block = audio_consumer_block,
        .stream = &g_audio_stream,
        .name = "agc_pipeline"
    }
};

static int audio_exec_init(const bm_exec_t *instance) {
    audio_axis_state_t *state = (audio_axis_state_t *)instance->state;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_audio_stream, _bm_stream_payload_g_audio_payloads,
                       AUDIO_BLOCK_DEPTH, sizeof(audio_pcm_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_audio_stream);
    rc = bm_pipeline_init(&g_audio_pipeline, g_audio_nodes,
                          (uint32_t)(sizeof(g_audio_nodes) /
                                     sizeof(g_audio_nodes[0])));
    return rc;
}

static int audio_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void audio_exec_safe_stop(const bm_exec_t *instance) {
    audio_axis_state_t *state = (audio_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_audio_ops = {
    audio_exec_init, audio_exec_start, audio_exec_safe_stop
};

static const bm_exec_t g_audio_exec = {
    1u, "audio_agc", &g_audio_state, NULL, NULL,
    g_audio_slots, 2u, NULL, 0u, &g_audio_ops
};

static const bm_exec_t *const g_instances[] = { &g_audio_exec };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { AUDIO_POLL_MS, EVENT_AUDIO_POLL, 1u, "tel_poll" }
};

void app_audio_enable_production(void) {
    g_audio_state.enabled = 1;
    g_audio_metrics.enable_events++;
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

    BM_LOGI(TAG, "stream_audio_agc example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_AUDIO_AGC: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);
    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_AUDIO_AGC: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_AUDIO_AGC: FAIL start\n");
        return 1;
    }
    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_AUDIO_AGC: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(45000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(AUDIO_QEMU_SIM_CYCLES);
#else
    run_sim(5000u);
#endif

    stats = bm_stream_stats(&g_audio_stream);
    if (g_audio_metrics.blocks_processed < AUDIO_PASS_MIN_BLOCKS ||
        g_audio_metrics.last_peak < AUDIO_PASS_LEVEL_MIN ||
        g_audio_metrics.enable_events == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        hybrid_print("EXAMPLE_STREAM_AUDIO_AGC: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u peak=%.3f",
            (unsigned)g_audio_metrics.blocks_processed,
            (double)g_audio_metrics.last_peak);
    hybrid_print_pass("STREAM_AUDIO_AGC");
    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

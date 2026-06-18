/**
 * @file main.c
 * @brief 振动块流示例：加速度正弦 → RMS + Goertzel 谱峰诊断
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
#include "app_vibration.h"
#include "bm/algorithm/bm_algo_spectral.h"
#include "bm/algorithm/bm_algo_statistics.h"
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

#define TAG "vib_diag"
#define VIB_TWO_PI  6.283185307f

#ifdef BM_EXAMPLE_QEMU
#define VIB_POLL_MS           20u
#define VIB_QEMU_SIM_CYCLES   60000u
#else
#define VIB_POLL_MS           100u
#endif

typedef struct {
    bm_algo_goertzel_config_t goertzel_cfg;
    bm_algo_goertzel_state_t  goertzel;
} vib_diag_node_state_t;

typedef struct {
    float    phase_rad;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    float    last_tone_mag;
    int      enabled;
} vib_axis_state_t;

vib_supervisor_metrics_t g_vib_metrics;

BM_STREAM_PAYLOADS(g_vib_payloads, vib_accel_block_t, VIB_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_vib_stream, VIB_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_vib_stream, VIB_BLOCK_DEPTH);

static vib_axis_state_t g_vib_state;
static vib_diag_node_state_t g_diag_node_state;
static bm_pipeline_t g_vib_pipeline;

static int vib_diag_prepare(void *state, const void *config) {
    vib_diag_node_state_t *st = (vib_diag_node_state_t *)state;
    const bm_algo_goertzel_config_t *cfg =
        (const bm_algo_goertzel_config_t *)config;

    if (st == NULL || cfg == NULL) {
        return BM_ERR_INVALID;
    }
    st->goertzel_cfg = *cfg;
    return bm_algo_goertzel_init(&st->goertzel, &st->goertzel_cfg) == 0 ?
           BM_OK : BM_ERR_INVALID;
}

static void vib_diag_reset(void *state) {
    vib_diag_node_state_t *st = (vib_diag_node_state_t *)state;

    if (st != NULL) {
        bm_algo_goertzel_reset(&st->goertzel);
    }
}

static int vib_diag_process(void *state,
                            const bm_block_t *input,
                            bm_block_t *output) {
    vib_diag_node_state_t *st = (vib_diag_node_state_t *)state;
    const vib_accel_block_t *in_payload;
    vib_accel_block_t *out_payload;
    uint32_t i;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const vib_accel_block_t *)input->data;
    out_payload = (vib_accel_block_t *)output->data;
    memcpy(out_payload->samples, in_payload->samples,
           sizeof(out_payload->samples));

    out_payload->block_rms = bm_algo_array_rms(
        in_payload->samples, VIB_SAMPLES_PER_BLOCK);

    bm_algo_goertzel_reset(&st->goertzel);
    for (i = 0u; i < VIB_SAMPLES_PER_BLOCK; ++i) {
        (void)bm_algo_goertzel_feed(&st->goertzel, &st->goertzel_cfg,
                                    in_payload->samples[i]);
    }
    out_payload->tone_magnitude = bm_algo_goertzel_result(
        &st->goertzel, &st->goertzel_cfg);
    return BM_OK;
}

static const bm_pipeline_node_ops_t g_vib_diag_ops = {
    vib_diag_prepare, vib_diag_process, vib_diag_reset, "vib_diag"
};

static const bm_algo_goertzel_config_t g_goertzel_cfg = {
    .target_freq_hz = VIB_TONE_FREQ_HZ,
    .sample_hz = (float)VIB_SAMPLE_RATE_HZ,
    .block_size = VIB_SAMPLES_PER_BLOCK,
    .coeff = 0.0f
};

static bm_pipeline_node_t g_vib_nodes[] = {
    {
        .ops = &g_vib_diag_ops,
        .state = &g_diag_node_state,
        .config = &g_goertzel_cfg,
        .input_format = VIB_FMT_RAW,
        .output_format = VIB_FMT_DIAG,
        .bypass = 0u
    }
};

static void fill_tone_block(vib_axis_state_t *state, vib_accel_block_t *payload) {
    uint32_t i;
    float step = VIB_TWO_PI * VIB_TONE_FREQ_HZ / (float)VIB_SAMPLE_RATE_HZ;

    for (i = 0u; i < VIB_SAMPLES_PER_BLOCK; ++i) {
        payload->samples[i] = VIB_INPUT_AMPLITUDE * sinf(state->phase_rad);
        state->phase_rad += step;
        if (state->phase_rad >= VIB_TWO_PI) {
            state->phase_rad -= VIB_TWO_PI;
        }
    }
    payload->block_rms = 0.0f;
    payload->tone_magnitude = 0.0f;
}

static void vib_producer_step(const bm_exec_t *instance) {
    vib_axis_state_t *state = (vib_axis_state_t *)instance->state;
    vib_accel_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;

    if (state == NULL || state->enabled == 0) {
        return;
    }
    if (bm_stream_producer_acquire(&g_vib_stream, &block) != BM_OK) {
        return;
    }

    payload = (vib_accel_block_t *)block->data;
    fill_tone_block(state, payload);
    block->format = VIB_FMT_RAW;
    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_vib_stream, block,
                                  (uint32_t)sizeof(vib_accel_block_t),
                                  &timestamp) != BM_OK) {
        return;
    }
    state->blocks_produced++;
    g_vib_metrics.blocks_produced = state->blocks_produced;
}

static void vib_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    vib_axis_state_t *state = (vib_axis_state_t *)instance->state;
    const vib_accel_block_t *payload;

    if (state == NULL || block == NULL) {
        return;
    }
    if (bm_pipeline_process_inplace(&g_vib_pipeline, block) != BM_OK) {
        (void)bm_stream_consumer_release(&g_vib_stream, block);
        return;
    }
    payload = (const vib_accel_block_t *)block->data;
    state->last_rms = payload->block_rms;
    state->last_tone_mag = payload->tone_magnitude;
    state->blocks_processed++;
    g_vib_metrics.blocks_processed = state->blocks_processed;
    g_vib_metrics.last_rms = payload->block_rms;
    g_vib_metrics.last_tone_mag = payload->tone_magnitude;
    (void)bm_stream_consumer_release(&g_vib_stream, block);
}

static const bm_exec_slot_t g_vib_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = VIB_BLOCK_PERIOD_US,
        .run = vib_producer_step,
        .name = "accel_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = VIB_BLOCK_PERIOD_US,
        .run_block = vib_consumer_block,
        .stream = &g_vib_stream,
        .name = "vib_pipeline"
    }
};

static int vib_exec_init(const bm_exec_t *instance) {
    vib_axis_state_t *state = (vib_axis_state_t *)instance->state;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_vib_stream, _bm_stream_payload_g_vib_payloads,
                       VIB_BLOCK_DEPTH, sizeof(vib_accel_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_vib_stream);
    rc = bm_pipeline_init(&g_vib_pipeline, g_vib_nodes,
                          (uint32_t)(sizeof(g_vib_nodes) /
                                     sizeof(g_vib_nodes[0])));
    return rc;
}

static int vib_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void vib_exec_safe_stop(const bm_exec_t *instance) {
    vib_axis_state_t *state = (vib_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_vib_ops = {
    vib_exec_init, vib_exec_start, vib_exec_safe_stop
};

static const bm_exec_t g_vib_exec = {
    1u, "vib_diag", &g_vib_state, NULL, NULL,
    g_vib_slots, 2u, NULL, 0u, &g_vib_ops
};

static const bm_exec_t *const g_instances[] = { &g_vib_exec };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { VIB_POLL_MS, EVENT_VIB_POLL, 1u, "tel_poll" }
};

void app_vibration_enable_production(void) {
    g_vib_state.enabled = 1;
    g_vib_metrics.enable_events++;
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

    BM_LOGI(TAG, "stream_vibration_diag example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VIBRATION_DIAG: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);
    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VIBRATION_DIAG: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VIBRATION_DIAG: FAIL start\n");
        return 1;
    }
    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VIBRATION_DIAG: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(45000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(VIB_QEMU_SIM_CYCLES);
#else
    run_sim(5000u);
#endif

    stats = bm_stream_stats(&g_vib_stream);
    if (g_vib_metrics.blocks_processed < VIB_PASS_MIN_BLOCKS ||
        g_vib_metrics.last_rms < VIB_PASS_RMS_MIN ||
        g_vib_metrics.last_tone_mag < VIB_PASS_TONE_MIN ||
        g_vib_metrics.enable_events == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        hybrid_print("EXAMPLE_STREAM_VIBRATION_DIAG: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u rms=%.3f tone=%.3f",
            (unsigned)g_vib_metrics.blocks_processed,
            (double)g_vib_metrics.last_rms,
            (double)g_vib_metrics.last_tone_mag);
    hybrid_print_pass("STREAM_VIBRATION_DIAG");
    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

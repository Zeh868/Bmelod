/**
 * @file main.c
 * @brief 块流示例：周期槽模拟 DMA 生产 + Block 槽 RMS 消费
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * E1 范围：bm_stream 零拷贝交接 + bm_exec Block 槽 + 块级 RMS；
 * 实机 DMA 由 bm_hal_dma_stream 对接（本示例用 Periodic 槽模拟填充）。
 */
#include "app_stream.h"
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

#define TAG "stream_rms"

#ifdef BM_EXAMPLE_QEMU
#define STREAM_POLL_MS            20u
#define STREAM_QEMU_SIM_CYCLES    60000u
#else
#define STREAM_POLL_MS            100u
#endif

/** 32 kHz 采样率，每块 32 点 → 1 ms 块周期 */
#define STREAM_SAMPLE_RATE_HZ     32000u
#define STREAM_SAMPLES_PER_BLOCK  32u
#define STREAM_BLOCK_PERIOD_US    1000u
#define STREAM_BLOCK_BYTES        (STREAM_SAMPLES_PER_BLOCK * sizeof(float))
#define STREAM_BLOCK_DEPTH        4u
#define STREAM_SINE_FREQ_HZ       1000.0f
#define STREAM_TWO_PI             6.283185307f

typedef struct {
    float    samples[STREAM_SAMPLES_PER_BLOCK];
} stream_pcm_block_t;

typedef struct {
    float    phase_rad;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_rms;
    uint32_t last_sequence;
    int      enabled;
} stream_axis_state_t;

stream_supervisor_metrics_t g_stream_metrics;

BM_STREAM_PAYLOADS(g_pcm_payloads, stream_pcm_block_t, STREAM_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_pcm_stream, STREAM_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_pcm_stream, STREAM_BLOCK_DEPTH);

static stream_axis_state_t g_stream_state;

static float compute_block_rms(const bm_block_t *block) {
    const stream_pcm_block_t *payload;
    uint32_t count;
    uint32_t i;
    float sum_sq = 0.0f;

    if (block == NULL || block->data == NULL || block->valid_bytes == 0u) {
        return 0.0f;
    }

    payload = (const stream_pcm_block_t *)block->data;
    count = block->valid_bytes / (uint32_t)sizeof(float);
    if (count > STREAM_SAMPLES_PER_BLOCK) {
        count = STREAM_SAMPLES_PER_BLOCK;
    }

    for (i = 0u; i < count; ++i) {
        float s = payload->samples[i];
        sum_sq += s * s;
    }
    if (count == 0u) {
        return 0.0f;
    }
    return sqrtf(sum_sq / (float)count);
}

/** 模拟 DMA 半缓冲完成：填充正弦 PCM 并提交块 */
static void stream_producer_step(const bm_exec_t *instance) {
    stream_axis_state_t *state = (stream_axis_state_t *)instance->state;
    stream_pcm_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;
    uint32_t i;
    float phase_step;

    if (state == NULL || state->enabled == 0) {
        return;
    }

    if (bm_stream_producer_acquire(&g_pcm_stream, &block) != BM_OK) {
        return;
    }

    payload = (stream_pcm_block_t *)block->data;
    phase_step = STREAM_TWO_PI * STREAM_SINE_FREQ_HZ /
                 (float)STREAM_SAMPLE_RATE_HZ;
    for (i = 0u; i < STREAM_SAMPLES_PER_BLOCK; ++i) {
        payload->samples[i] =
            STREAM_SINE_AMPLITUDE * sinf(state->phase_rad);
        state->phase_rad += phase_step;
        if (state->phase_rad >= STREAM_TWO_PI) {
            state->phase_rad -= STREAM_TWO_PI;
        }
    }

    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_pcm_stream, block, STREAM_BLOCK_BYTES,
                                  &timestamp) != BM_OK) {
        BM_LOGW(TAG, "producer commit failed");
        return;
    }

    state->blocks_produced++;
    g_stream_metrics.blocks_produced = state->blocks_produced;
}

/** Block RT：块就绪后计算 RMS 并归还缓冲 */
static void stream_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    stream_axis_state_t *state = (stream_axis_state_t *)instance->state;
    float rms;

    if (state == NULL || block == NULL) {
        return;
    }

    rms = compute_block_rms(block);
    state->last_rms = rms;
    state->blocks_processed++;
    if (state->last_sequence != 0u &&
        block->sequence != state->last_sequence + 1u) {
        BM_LOGW(TAG, "sequence gap: last=%u got=%u",
                (unsigned)state->last_sequence, (unsigned)block->sequence);
    }
    state->last_sequence = block->sequence;

    g_stream_metrics.blocks_processed = state->blocks_processed;
    g_stream_metrics.last_rms = rms;

    (void)bm_stream_consumer_release(&g_pcm_stream, block);
}

static const bm_exec_slot_t g_stream_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = STREAM_BLOCK_PERIOD_US,
        .run = stream_producer_step,
        .name = "dma_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = STREAM_BLOCK_PERIOD_US,
        .run_block = stream_consumer_block,
        .stream = &g_pcm_stream,
        .name = "rms_block"
    }
};

static int stream_exec_init(const bm_exec_t *instance) {
    stream_axis_state_t *state = (stream_axis_state_t *)instance->state;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_pcm_stream, _bm_stream_payload_g_pcm_payloads,
                       STREAM_BLOCK_DEPTH, sizeof(stream_pcm_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_pcm_stream);
    return BM_OK;
}

static int stream_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void app_stream_enable_production(void) {
    g_stream_state.enabled = 1;
    g_stream_metrics.enable_events++;
}

static void stream_exec_safe_stop(const bm_exec_t *instance) {
    stream_axis_state_t *state = (stream_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_stream_ops = {
    stream_exec_init, stream_exec_start, stream_exec_safe_stop
};

static const bm_exec_t g_stream_exec_bound = {
    1u,
    "stream_rms",
    &g_stream_state,
    NULL,
    NULL,
    g_stream_slots,
    2u,
    NULL,
    0u,
    &g_stream_ops
};

static const bm_exec_t *const g_instances[] = { &g_stream_exec_bound };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { STREAM_POLL_MS, EVENT_STREAM_POLL, 1u, "tel_poll" }
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

    BM_LOGI(TAG, "stream_block_rms example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_BLOCK_RMS: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_BLOCK_RMS: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_STREAM_BLOCK_RMS: FAIL start\n");
        return 1;
    }

    (void)bm_event_process(8u);

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_STREAM_BLOCK_RMS: FAIL ticker\n");
        return 1;
    }

#ifdef NATIVE_SIM
    run_sim(15000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(STREAM_QEMU_SIM_CYCLES);
#else
    run_sim(4000u);
#endif

#ifdef BM_EXAMPLE_QEMU
    if (g_stream_metrics.enable_events == 0u) {
        (void)bm_event_publish_copy(EVENT_STREAM_ENABLE, 1u, NULL, 0u);
        (void)bm_event_process(8u);
        run_sim(4000u);
    }
    (void)bm_ticker_poll();
    (void)bm_event_process(8u);
#endif

    stats = bm_stream_stats(&g_pcm_stream);
    rms_err = fabsf(g_stream_metrics.last_rms - STREAM_EXPECTED_RMS);

    if (g_stream_metrics.blocks_processed < STREAM_PASS_MIN_BLOCKS ||
        rms_err > STREAM_RMS_PASS_TOLERANCE ||
        g_stream_metrics.enable_events == 0u ||
        g_stream_metrics.telemetry_reads == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        BM_LOGE(TAG, "fail: proc=%u rms=%.4f err=%.4f en=%u tel=%u corrupt=%u",
                (unsigned)g_stream_metrics.blocks_processed,
                (double)g_stream_metrics.last_rms, (double)rms_err,
                (unsigned)g_stream_metrics.enable_events,
                (unsigned)g_stream_metrics.telemetry_reads,
                stats != NULL ? (unsigned)stats->corrupt : 0u);
        hybrid_print("EXAMPLE_STREAM_BLOCK_RMS: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u rms=%.4f overrun=%u",
            (unsigned)g_stream_metrics.blocks_processed,
            (double)g_stream_metrics.last_rms,
            (unsigned)stats->overrun);
    hybrid_print_pass("STREAM_BLOCK_RMS");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

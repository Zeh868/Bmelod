/**
 * @file main.c
 * @brief 块流 FFT 示例：DMA 模拟生产 + Block 槽 RFFT 频谱峰
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * E1 范围：bm_hal_dma_stream（native_sim）+ bm_stream + bm_exec Block 槽 +
 * bm_algo_rfft；验收 1 kHz 正弦在 bin 4 出现主峰。
 */
#include "app_fft.h"
#include "bm/algorithm/bm_algo_fft.h"
#include "bm_event.h"
#include "bm_exec.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_stream.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "bm_hal_dma_stream_sim.h"
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

#define TAG "stream_fft"

#ifdef BM_EXAMPLE_QEMU
#define FFT_POLL_MS            20u
/** 4 ms 块周期 × 50 块 ≈ 200 ms HRT，约为 stream_block_rms（1 ms×80）的 2.5× */
#define FFT_QEMU_SIM_CYCLES    150000u
#else
#define FFT_POLL_MS            100u
#endif

/** 16 kHz 采样，64 点/块 → 4 ms 块周期 */
#define FFT_SAMPLE_RATE_HZ        16000u
#define FFT_SAMPLES_PER_BLOCK     64u
#define FFT_BLOCK_PERIOD_US       4000u
#define FFT_BLOCK_BYTES           (FFT_SAMPLES_PER_BLOCK * sizeof(float))
#define FFT_BLOCK_DEPTH           4u
#define FFT_SIZE                  BM_ALGO_FFT_SIZE_64
#define FFT_SINE_FREQ_HZ          1000.0f
#define FFT_SINE_AMPLITUDE        1.0f
#define FFT_TWO_PI                6.283185307f

typedef struct {
    float samples[FFT_SAMPLES_PER_BLOCK];
} fft_pcm_block_t;

typedef struct {
    float    phase_rad;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    uint32_t peak_bin;
    float    peak_mag;
    int      enabled;
    bm_algo_rfft_f32_t rfft;
    float    rfft_work[FFT_SIZE * 2u];
    float    spectrum[FFT_SIZE / 2u + 1u];
} fft_axis_state_t;

fft_supervisor_metrics_t g_fft_metrics;

BM_STREAM_PAYLOADS(g_pcm_payloads, fft_pcm_block_t, FFT_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_pcm_stream, FFT_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_pcm_stream, FFT_BLOCK_DEPTH);

static fft_axis_state_t g_fft_state;

static void fill_block_timestamp(bm_timestamp_t *timestamp) {
    timestamp->clock_id = 0u;
    timestamp->quality = 1u;
    timestamp->rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp->ticks = bm_hal_timer_get_ticks();
}

void app_fft_enable_production(void) {
    g_fft_state.enabled = 1;
    g_fft_metrics.enable_events++;
}

/** 向块缓冲写入正弦 PCM */
static void fill_sine_block(fft_axis_state_t *state, fft_pcm_block_t *payload) {
    uint32_t i;
    float phase_step;

    phase_step = FFT_TWO_PI * FFT_SINE_FREQ_HZ / (float)FFT_SAMPLE_RATE_HZ;
    for (i = 0u; i < FFT_SAMPLES_PER_BLOCK; ++i) {
        payload->samples[i] = FFT_SINE_AMPLITUDE * sinf(state->phase_rad);
        state->phase_rad += phase_step;
        if (state->phase_rad >= FFT_TWO_PI) {
            state->phase_rad -= FFT_TWO_PI;
        }
    }
}

/** DMA RX 完成：填充样本并提交到 bm_stream */
static void on_dma_rx_full(const bm_hal_dma_stream_t *stream,
                           bm_block_t *block,
                           void *context) {
    fft_axis_state_t *state = (fft_axis_state_t *)context;
    fft_pcm_block_t *payload;
    bm_timestamp_t timestamp;

    (void)stream;
    if (state == NULL || block == NULL || block->data == NULL) {
        return;
    }

    payload = (fft_pcm_block_t *)block->data;
    fill_sine_block(state, payload);
    fill_block_timestamp(&timestamp);

    if (bm_stream_producer_commit(&g_pcm_stream, block, FFT_BLOCK_BYTES,
                                  &timestamp) != BM_OK) {
        (void)bm_stream_producer_abort(&g_pcm_stream, block);
        return;
    }
    state->blocks_produced++;
    g_fft_metrics.blocks_produced = state->blocks_produced;
}

/** 在频谱幅值数组中查找主峰 bin（跳过 DC） */
static uint32_t find_peak_bin(const float *spectrum, uint32_t count) {
    uint32_t peak_bin = 1u;
    uint32_t i;

    for (i = 2u; i < count; ++i) {
        if (spectrum[i] > spectrum[peak_bin]) {
            peak_bin = i;
        }
    }
    return peak_bin;
}

/** Block RT：对块内 PCM 做 RFFT 并更新峰值指标 */
static void fft_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    fft_axis_state_t *state = (fft_axis_state_t *)instance->state;
    const fft_pcm_block_t *payload;
    uint32_t peak_bin;

    if (state == NULL || block == NULL || block->data == NULL) {
        return;
    }

    payload = (const fft_pcm_block_t *)block->data;
    if (bm_algo_rfft_f32_execute(&state->rfft, payload->samples,
                                 state->spectrum) != 0) {
        (void)bm_stream_consumer_release(&g_pcm_stream, block);
        return;
    }

    peak_bin = find_peak_bin(state->spectrum, FFT_SIZE / 2u + 1u);
    state->peak_bin = peak_bin;
    state->peak_mag = state->spectrum[peak_bin];
    state->blocks_processed++;

    g_fft_metrics.peak_bin = peak_bin;
    g_fft_metrics.peak_mag = state->peak_mag;
    g_fft_metrics.blocks_processed = state->blocks_processed;

    (void)bm_stream_consumer_release(&g_pcm_stream, block);
}

/** 周期槽：模拟 DMA 提交并完成一次块接收 */
static void dma_producer_step(const bm_exec_t *instance) {
    fft_axis_state_t *state = (fft_axis_state_t *)instance->state;
    bm_block_t *block;

    if (state == NULL || state->enabled == 0) {
        return;
    }

    if (bm_stream_producer_acquire(&g_pcm_stream, &block) != BM_OK) {
        return;
    }
    if (bm_hal_dma_stream_submit_rx(&BM_HAL_DMA_SIM0, block) != BM_OK) {
        (void)bm_stream_producer_abort(&g_pcm_stream, block);
        return;
    }
    bm_hal_dma_stream_sim_fire_rx_complete(&BM_HAL_DMA_SIM0);
}

static const bm_exec_slot_t g_fft_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = FFT_BLOCK_PERIOD_US,
        .run = dma_producer_step,
        .name = "dma_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = FFT_BLOCK_PERIOD_US,
        .run_block = fft_consumer_block,
        .stream = &g_pcm_stream,
        .name = "fft_block"
    }
};

static int fft_exec_init(const bm_exec_t *instance) {
    fft_axis_state_t *state = (fft_axis_state_t *)instance->state;
    bm_hal_dma_stream_binding_t dma_binding;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_pcm_stream, _bm_stream_payload_g_pcm_payloads,
                       FFT_BLOCK_DEPTH, sizeof(fft_pcm_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_pcm_stream);
    if (bm_algo_rfft_f32_init(&state->rfft, FFT_SIZE, state->rfft_work,
                              (uint32_t)(sizeof(state->rfft_work) /
                                         sizeof(float))) != 0) {
        return BM_ERR_INVALID;
    }

    dma_binding.on_half = NULL;
    dma_binding.on_full = on_dma_rx_full;
    dma_binding.context = state;
    if (bm_hal_dma_stream_bind_rx(&BM_HAL_DMA_SIM0, &dma_binding) != BM_OK) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

static int fft_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void fft_exec_safe_stop(const bm_exec_t *instance) {
    fft_axis_state_t *state = (fft_axis_state_t *)instance->state;
    bm_block_t *block;

    if (state != NULL) {
        state->enabled = 0;
    }
    block = bm_hal_dma_stream_detach_rx(&BM_HAL_DMA_SIM0);
    if (block != NULL) {
        (void)bm_stream_producer_abort(&g_pcm_stream, block);
    }
}

static const bm_exec_ops_t g_fft_ops = {
    fft_exec_init, fft_exec_start, fft_exec_safe_stop
};

static const bm_exec_t g_fft_exec = {
    1u,
    "stream_fft",
    &g_fft_state,
    NULL,
    NULL,
    g_fft_slots,
    2u,
    NULL,
    0u,
    &g_fft_ops
};

static const bm_exec_t *const g_instances[] = { &g_fft_exec };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { FFT_POLL_MS, EVENT_FFT_POLL, 1u, "tel_poll" }
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

    BM_LOGI(TAG, "stream_fft 示例启动");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_FFT: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_FFT: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_STREAM_FFT: FAIL start\n");
        return 1;
    }

    (void)bm_event_process(8u);

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_STREAM_FFT: FAIL ticker\n");
        return 1;
    }

#ifdef NATIVE_SIM
    run_sim(12000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(FFT_QEMU_SIM_CYCLES);
#else
    run_sim(3000u);
#endif

#ifdef BM_EXAMPLE_QEMU
    if (g_fft_metrics.enable_events == 0u) {
        (void)bm_event_publish_copy(EVENT_FFT_ENABLE, 1u, NULL, 0u);
        (void)bm_event_process(8u);
        run_sim(10000u);
    }
    (void)bm_ticker_poll();
    (void)bm_event_process(8u);
#endif

    stats = bm_stream_stats(&g_pcm_stream);
    if (g_fft_metrics.blocks_processed < FFT_PASS_MIN_BLOCKS ||
        g_fft_metrics.peak_bin != FFT_EXPECTED_PEAK_BIN ||
        g_fft_metrics.peak_mag < FFT_PEAK_MAG_MIN ||
        g_fft_metrics.telemetry_reads == 0u ||
        g_fft_metrics.enable_events == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        BM_LOGE(TAG,
                "验收失败: proc=%u peak_bin=%u mag=%.3f tel=%u en=%u corrupt=%u",
                (unsigned)g_fft_metrics.blocks_processed,
                (unsigned)g_fft_metrics.peak_bin,
                (double)g_fft_metrics.peak_mag,
                (unsigned)g_fft_metrics.telemetry_reads,
                (unsigned)g_fft_metrics.enable_events,
                stats != NULL ? (unsigned)stats->corrupt : 0u);
        hybrid_print("EXAMPLE_STREAM_FFT: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "验收通过: blocks=%u peak_bin=%u mag=%.3f late=%u",
            (unsigned)g_fft_metrics.blocks_processed,
            (unsigned)g_fft_metrics.peak_bin,
            (double)g_fft_metrics.peak_mag,
            (unsigned)stats->late);
    hybrid_print_pass("STREAM_FFT");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

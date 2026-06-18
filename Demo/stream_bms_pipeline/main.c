/**
 * @file main.c
 * @brief BMS 块流示例：DMA 模拟生产 + bm_pipeline（LPF→库仑 SOC）
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * M6 E1：Pack 电流块流经线性管线估算 SOC，验证 bm_stream + bm_pipeline 组合。
 */
#include "app_bms_pipeline.h"
#include "bm/algorithm/bm_algo_battery.h"
#include "bm/algorithm/bm_algo_filter.h"
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

#include <stdint.h>
#include <string.h>

#define TAG "bms_pipe"

#ifdef BM_EXAMPLE_QEMU
#define BMS_POLL_MS            20u
#define BMS_QEMU_SIM_CYCLES    60000u
#else
#define BMS_POLL_MS            100u
#endif

typedef struct {
    bm_algo_lpf1_config_t  cfg;
    bm_algo_lpf1_state_t   lpf;
} bms_lpf_node_state_t;

typedef struct {
    bm_algo_coulomb_config_t  cfg;
    bm_algo_coulomb_state_t   coulomb;
} bms_coulomb_node_state_t;

typedef struct {
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    float    last_soc;
    int      enabled;
} bms_axis_state_t;

bms_supervisor_metrics_t g_bms_metrics;

BM_STREAM_PAYLOADS(g_pack_payloads, bms_pack_block_t, BMS_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_pack_stream, BMS_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_pack_stream, BMS_BLOCK_DEPTH);

static bms_axis_state_t g_bms_state;
static bms_lpf_node_state_t g_lpf_node_state;
static bms_coulomb_node_state_t g_coulomb_node_state;
static bm_pipeline_t g_bms_pipeline;

static const bm_algo_coulomb_config_t g_coulomb_cfg = {
    .nominal_capacity_ah = BMS_NOMINAL_CAPACITY_AH,
    .coulomb_efficiency = 1.0f,
    .soc_min = 0.0f,
    .soc_max = 1.0f
};

static int bms_lpf_prepare(void *state, const void *config) {
    bms_lpf_node_state_t *st = (bms_lpf_node_state_t *)state;
    (void)config;

    if (st == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_lpf1_init_from_cutoff(&st->cfg, 50.0f,
                                      (float)BMS_SAMPLE_RATE_HZ) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_algo_lpf1_reset(&st->lpf, BMS_PACK_CHARGE_A);
    return BM_OK;
}

static void bms_lpf_reset(void *state) {
    bms_lpf_node_state_t *st = (bms_lpf_node_state_t *)state;

    if (st != NULL) {
        bm_algo_lpf1_reset(&st->lpf, BMS_PACK_CHARGE_A);
    }
}

/** 节点 1：对块内 Pack 电流逐点一阶低通 */
static int bms_lpf_process(void *state,
                           const bm_block_t *input,
                           bm_block_t *output) {
    bms_lpf_node_state_t *st = (bms_lpf_node_state_t *)state;
    const bms_pack_block_t *in_payload;
    bms_pack_block_t *out_payload;
    uint32_t i;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const bms_pack_block_t *)input->data;
    out_payload = (bms_pack_block_t *)output->data;
    out_payload->soc = in_payload->soc;

    for (i = 0u; i < BMS_SAMPLES_PER_BLOCK; ++i) {
        out_payload->current_a[i] =
            bm_algo_lpf1_step(&st->lpf, &st->cfg, in_payload->current_a[i]);
    }
    return BM_OK;
}

static int bms_coulomb_prepare(void *state, const void *config) {
    bms_coulomb_node_state_t *st = (bms_coulomb_node_state_t *)state;
    const bm_algo_coulomb_config_t *cfg =
        (const bm_algo_coulomb_config_t *)config;

    if (st == NULL || cfg == NULL) {
        return BM_ERR_INVALID;
    }
    st->cfg = *cfg;
    bm_algo_coulomb_reset(&st->coulomb, BMS_SOC_INIT);
    return BM_OK;
}

static void bms_coulomb_reset(void *state) {
    bms_coulomb_node_state_t *st = (bms_coulomb_node_state_t *)state;

    if (st != NULL) {
        bm_algo_coulomb_reset(&st->coulomb, BMS_SOC_INIT);
    }
}

/** 节点 2：对滤波电流块积分并写回 SOC */
static int bms_coulomb_process(void *state,
                               const bm_block_t *input,
                               bm_block_t *output) {
    bms_coulomb_node_state_t *st = (bms_coulomb_node_state_t *)state;
    const bms_pack_block_t *in_payload;
    bms_pack_block_t *out_payload;
    uint32_t i;
    float soc;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const bms_pack_block_t *)input->data;
    out_payload = (bms_pack_block_t *)output->data;

    for (i = 0u; i < BMS_SAMPLES_PER_BLOCK; ++i) {
        soc = bm_algo_coulomb_step(&st->coulomb, &st->cfg,
                                   in_payload->current_a[i], BMS_SAMPLE_DT_S);
        out_payload->current_a[i] = in_payload->current_a[i];
    }
    out_payload->soc = soc;
    return BM_OK;
}

static const bm_pipeline_node_ops_t g_bms_lpf_ops = {
    bms_lpf_prepare, bms_lpf_process, bms_lpf_reset, "pack_lpf"
};

static const bm_pipeline_node_ops_t g_bms_coulomb_ops = {
    bms_coulomb_prepare, bms_coulomb_process, bms_coulomb_reset, "coulomb_soc"
};

static bm_pipeline_node_t g_bms_nodes[] = {
    {
        .ops = &g_bms_lpf_ops,
        .state = &g_lpf_node_state,
        .config = NULL,
        .input_format = BMS_FMT_RAW,
        .output_format = BMS_FMT_FILTERED,
        .bypass = 0u
    },
    {
        .ops = &g_bms_coulomb_ops,
        .state = &g_coulomb_node_state,
        .config = &g_coulomb_cfg,
        .input_format = BMS_FMT_FILTERED,
        .output_format = BMS_FMT_SOC,
        .bypass = 0u
    }
};

/** 模拟 DMA：填充恒定充电电流块并提交 */
static void bms_producer_step(const bm_exec_t *instance) {
    bms_axis_state_t *state = (bms_axis_state_t *)instance->state;
    bms_pack_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;
    uint32_t i;

    if (state == NULL || state->enabled == 0) {
        return;
    }

    if (bm_stream_producer_acquire(&g_pack_stream, &block) != BM_OK) {
        return;
    }

    payload = (bms_pack_block_t *)block->data;
    payload->soc = BMS_SOC_INIT;
    for (i = 0u; i < BMS_SAMPLES_PER_BLOCK; ++i) {
        payload->current_a[i] = BMS_PACK_CHARGE_A;
    }
    block->format = BMS_FMT_RAW;

    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_pack_stream, block,
                                  (uint32_t)sizeof(bms_pack_block_t),
                                  &timestamp) != BM_OK) {
        BM_LOGW(TAG, "producer commit failed");
        return;
    }

    state->blocks_produced++;
    g_bms_metrics.blocks_produced = state->blocks_produced;
}

/** Block RT：管线处理块并归还缓冲 */
static void bms_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    bms_axis_state_t *state = (bms_axis_state_t *)instance->state;
    const bms_pack_block_t *payload;
    int rc;

    if (state == NULL || block == NULL) {
        return;
    }

    rc = bm_pipeline_process_inplace(&g_bms_pipeline, block);
    if (rc != BM_OK) {
        BM_LOGW(TAG, "pipeline process failed rc=%d", rc);
        (void)bm_stream_consumer_release(&g_pack_stream, block);
        return;
    }

    payload = (const bms_pack_block_t *)block->data;
    state->last_soc = payload->soc;
    state->blocks_processed++;
    g_bms_metrics.blocks_processed = state->blocks_processed;
    g_bms_metrics.last_soc = payload->soc;

    (void)bm_stream_consumer_release(&g_pack_stream, block);
}

static const bm_exec_slot_t g_bms_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = BMS_BLOCK_PERIOD_US,
        .run = bms_producer_step,
        .name = "dma_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = BMS_BLOCK_PERIOD_US,
        .run_block = bms_consumer_block,
        .stream = &g_pack_stream,
        .name = "bms_pipeline"
    }
};

static int bms_exec_init(const bm_exec_t *instance) {
    bms_axis_state_t *state = (bms_axis_state_t *)instance->state;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));

    if (bm_stream_init(&g_pack_stream, _bm_stream_payload_g_pack_payloads,
                       BMS_BLOCK_DEPTH, sizeof(bms_pack_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_pack_stream);

    rc = bm_pipeline_init(&g_bms_pipeline, g_bms_nodes,
                          (uint32_t)(sizeof(g_bms_nodes) /
                                     sizeof(g_bms_nodes[0])));
    if (rc != BM_OK) {
        return rc;
    }
    return BM_OK;
}

static int bms_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

void app_bms_enable_production(void) {
    g_bms_state.enabled = 1;
    g_bms_metrics.enable_events++;
}

static void bms_exec_safe_stop(const bm_exec_t *instance) {
    bms_axis_state_t *state = (bms_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_bms_ops = {
    bms_exec_init, bms_exec_start, bms_exec_safe_stop
};

static const bm_exec_t g_bms_exec = {
    1u,
    "bms_pipeline",
    &g_bms_state,
    NULL,
    NULL,
    g_bms_slots,
    2u,
    NULL,
    0u,
    &g_bms_ops
};

static const bm_exec_t *const g_instances[] = { &g_bms_exec };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { BMS_POLL_MS, EVENT_BMS_POLL, 1u, "tel_poll" }
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

    BM_LOGI(TAG, "stream_bms_pipeline example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_BMS_PIPELINE: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_BMS_PIPELINE: FAIL init\n");
        return 1;
    }

    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_STREAM_BMS_PIPELINE: FAIL start\n");
        return 1;
    }

    (void)bm_event_process(8u);

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_STREAM_BMS_PIPELINE: FAIL ticker\n");
        return 1;
    }

#ifdef NATIVE_SIM
    run_sim(45000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(BMS_QEMU_SIM_CYCLES);
#else
    run_sim(5000u);
#endif

#ifdef BM_EXAMPLE_QEMU
    if (g_bms_metrics.enable_events == 0u) {
        (void)bm_event_publish_copy(EVENT_BMS_ENABLE, 1u, NULL, 0u);
        (void)bm_event_process(8u);
        run_sim(4000u);
    }
    (void)bm_ticker_poll();
    (void)bm_event_process(8u);
#endif

    stats = bm_stream_stats(&g_pack_stream);

    if (g_bms_metrics.blocks_processed < BMS_PASS_MIN_BLOCKS ||
        g_bms_metrics.last_soc < BMS_SOC_PASS_MIN ||
        g_bms_metrics.enable_events == 0u ||
        g_bms_metrics.telemetry_reads == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        BM_LOGE(TAG, "fail: proc=%u soc=%.4f en=%u tel=%u corrupt=%u",
                (unsigned)g_bms_metrics.blocks_processed,
                (double)g_bms_metrics.last_soc,
                (unsigned)g_bms_metrics.enable_events,
                (unsigned)g_bms_metrics.telemetry_reads,
                stats != NULL ? (unsigned)stats->corrupt : 0u);
        hybrid_print("EXAMPLE_STREAM_BMS_PIPELINE: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u soc=%.4f overrun=%u",
            (unsigned)g_bms_metrics.blocks_processed,
            (double)g_bms_metrics.last_soc,
            (unsigned)stats->overrun);
    hybrid_print_pass("STREAM_BMS_PIPELINE");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

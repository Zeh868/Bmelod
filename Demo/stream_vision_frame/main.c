/**
 * @file main.c
 * @brief 视觉帧流示例：移动亮块 → 帧差 → 质心，验证 bm_pipeline + bm_algo_image/vision
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
#include "app_vision.h"
#include "bm/algorithm/bm_algo_image.h"
#include "bm/algorithm/bm_algo_vision.h"
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

#define TAG "vision_frame"

#ifdef BM_EXAMPLE_QEMU
#define VISION_POLL_MS           20u
#define VISION_QEMU_SIM_CYCLES   60000u
#else
#define VISION_POLL_MS           100u
#endif

typedef struct {
    uint8_t  prev_pixels[VISION_FRAME_PIXELS];
    uint8_t  have_prev;
} vision_diff_node_state_t;

typedef struct {
    uint32_t box_x;
    uint32_t box_y;
    uint32_t blocks_produced;
    uint32_t blocks_processed;
    int      enabled;
} vision_axis_state_t;

vision_supervisor_metrics_t g_vision_metrics;

BM_STREAM_PAYLOADS(g_vision_payloads, vision_frame_block_t, VISION_BLOCK_DEPTH);
BM_STREAM_BLOCKS(g_vision_stream, VISION_BLOCK_DEPTH);
BM_STREAM_INSTANCE(g_vision_stream, VISION_BLOCK_DEPTH);

static vision_axis_state_t g_vision_state;
static vision_diff_node_state_t g_diff_node_state;
static bm_pipeline_t g_vision_pipeline;

static int vision_diff_prepare(void *state, const void *config) {
    vision_diff_node_state_t *st = (vision_diff_node_state_t *)state;

    (void)config;
    if (st == NULL) {
        return BM_ERR_INVALID;
    }
    memset(st->prev_pixels, 0, sizeof(st->prev_pixels));
    st->have_prev = 0u;
    return BM_OK;
}

static void vision_diff_reset(void *state) {
    vision_diff_node_state_t *st = (vision_diff_node_state_t *)state;

    if (st != NULL) {
        st->have_prev = 0u;
    }
}

static int vision_diff_process(void *state,
                               const bm_block_t *input,
                               bm_block_t *output) {
    vision_diff_node_state_t *st = (vision_diff_node_state_t *)state;
    const vision_frame_block_t *in_payload;
    vision_frame_block_t *out_payload;

    if (st == NULL || input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const vision_frame_block_t *)input->data;
    out_payload = (vision_frame_block_t *)output->data;
    memcpy(out_payload->pixels, in_payload->pixels, sizeof(out_payload->pixels));

    if (st->have_prev != 0u) {
        bm_algo_image_frame_diff_u8(st->prev_pixels, in_payload->pixels,
                                    out_payload->diff_mask,
                                    VISION_FRAME_WIDTH, VISION_FRAME_HEIGHT,
                                    VISION_DIFF_THRESH);
        out_payload->motion_detected = 1u;
    } else {
        memset(out_payload->diff_mask, 0, sizeof(out_payload->diff_mask));
        out_payload->motion_detected = 0u;
    }

    memcpy(st->prev_pixels, in_payload->pixels, sizeof(st->prev_pixels));
    st->have_prev = 1u;
    return BM_OK;
}

static int vision_centroid_prepare(void *state, const void *config) {
    (void)state;
    (void)config;
    return BM_OK;
}

static void vision_centroid_reset(void *state) {
    (void)state;
}

static int vision_centroid_process(void *state,
                                   const bm_block_t *input,
                                   bm_block_t *output) {
    const vision_frame_block_t *in_payload;
    vision_frame_block_t *out_payload;
    float cx = 0.0f;
    float cy = 0.0f;
    int rc;

    (void)state;
    if (input == NULL || output == NULL ||
        input->data == NULL || output->data == NULL) {
        return BM_ERR_INVALID;
    }

    in_payload = (const vision_frame_block_t *)input->data;
    out_payload = (vision_frame_block_t *)output->data;
    memcpy(out_payload, in_payload, sizeof(*out_payload));

    out_payload->centroid_x = 0.0f;
    out_payload->centroid_y = 0.0f;
    if (out_payload->motion_detected == 0u) {
        return BM_OK;
    }

    rc = bm_algo_vision_centroid_u8(out_payload->diff_mask,
                                    VISION_FRAME_WIDTH, VISION_FRAME_HEIGHT,
                                    &cx, &cy);
    if (rc == 0) {
        out_payload->centroid_x = cx;
        out_payload->centroid_y = cy;
    } else {
        out_payload->motion_detected = 0u;
    }
    return BM_OK;
}

static const bm_pipeline_node_ops_t g_vision_diff_ops = {
    vision_diff_prepare, vision_diff_process, vision_diff_reset, "frame_diff"
};

static const bm_pipeline_node_ops_t g_vision_centroid_ops = {
    vision_centroid_prepare, vision_centroid_process, vision_centroid_reset,
    "centroid"
};

static bm_pipeline_node_t g_vision_nodes[] = {
    {
        .ops = &g_vision_diff_ops,
        .state = &g_diff_node_state,
        .config = NULL,
        .input_format = VISION_FMT_RAW,
        .output_format = VISION_FMT_DIFF,
        .bypass = 0u
    },
    {
        .ops = &g_vision_centroid_ops,
        .state = NULL,
        .config = NULL,
        .input_format = VISION_FMT_DIFF,
        .output_format = VISION_FMT_TRACKED,
        .bypass = 0u
    }
};

static void fill_frame_block(vision_axis_state_t *state, vision_frame_block_t *payload) {
    uint32_t x;
    uint32_t y;

    memset(payload->pixels, 0, sizeof(payload->pixels));
    for (y = 0u; y < VISION_BOX_SIZE; ++y) {
        for (x = 0u; x < VISION_BOX_SIZE; ++x) {
            uint32_t px = state->box_x + x;
            uint32_t py = state->box_y + y;
            if (px < VISION_FRAME_WIDTH && py < VISION_FRAME_HEIGHT) {
                payload->pixels[py * VISION_FRAME_WIDTH + px] = 220u;
            }
        }
    }

    state->box_x += 1u;
    if (state->box_x + VISION_BOX_SIZE >= VISION_FRAME_WIDTH) {
        state->box_x = 0u;
        state->box_y = (state->box_y + 1u) %
                       (VISION_FRAME_HEIGHT - VISION_BOX_SIZE);
    }

    payload->diff_mask[0] = 0u;
    payload->centroid_x = 0.0f;
    payload->centroid_y = 0.0f;
    payload->motion_detected = 0u;
}

static void vision_producer_step(const bm_exec_t *instance) {
    vision_axis_state_t *state = (vision_axis_state_t *)instance->state;
    vision_frame_block_t *payload;
    bm_block_t *block;
    bm_timestamp_t timestamp;

    if (state == NULL || state->enabled == 0) {
        return;
    }
    if (bm_stream_producer_acquire(&g_vision_stream, &block) != BM_OK) {
        return;
    }

    payload = (vision_frame_block_t *)block->data;
    fill_frame_block(state, payload);
    block->format = VISION_FMT_RAW;
    timestamp.clock_id = 0u;
    timestamp.quality = 1u;
    timestamp.rate_hz = 1000000u / BM_CONFIG_HRT_TICK_US;
    timestamp.ticks = bm_hal_timer_get_ticks();

    if (bm_stream_producer_commit(&g_vision_stream, block,
                                  (uint32_t)sizeof(vision_frame_block_t),
                                  &timestamp) != BM_OK) {
        return;
    }
    state->blocks_produced++;
    g_vision_metrics.blocks_produced = state->blocks_produced;
}

static void vision_consumer_block(const bm_exec_t *instance, bm_block_t *block) {
    vision_axis_state_t *state = (vision_axis_state_t *)instance->state;
    const vision_frame_block_t *payload;

    if (state == NULL || block == NULL) {
        return;
    }
    if (bm_pipeline_process_inplace(&g_vision_pipeline, block) != BM_OK) {
        (void)bm_stream_consumer_release(&g_vision_stream, block);
        return;
    }

    payload = (const vision_frame_block_t *)block->data;
    state->blocks_processed++;
    g_vision_metrics.blocks_processed = state->blocks_processed;
    g_vision_metrics.last_cx = payload->centroid_x;
    g_vision_metrics.last_cy = payload->centroid_y;
    if (payload->motion_detected != 0u) {
        g_vision_metrics.motion_frames++;
    }
    (void)bm_stream_consumer_release(&g_vision_stream, block);
}

static const bm_exec_slot_t g_vision_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = VISION_BLOCK_PERIOD_US,
        .run = vision_producer_step,
        .name = "frame_sim"
    },
    {
        .kind = BM_EXEC_SLOT_BLOCK,
        .deadline_us = VISION_BLOCK_PERIOD_US,
        .run_block = vision_consumer_block,
        .stream = &g_vision_stream,
        .name = "vision_pipeline"
    }
};

static int vision_exec_init(const bm_exec_t *instance) {
    vision_axis_state_t *state = (vision_axis_state_t *)instance->state;
    int rc;

    if (state == NULL) {
        return BM_ERR_INVALID;
    }
    memset(state, 0, sizeof(*state));
    if (bm_stream_init(&g_vision_stream, _bm_stream_payload_g_vision_payloads,
                       VISION_BLOCK_DEPTH, sizeof(vision_frame_block_t)) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_stream_reset(&g_vision_stream);
    rc = bm_pipeline_init(&g_vision_pipeline, g_vision_nodes,
                          (uint32_t)(sizeof(g_vision_nodes) /
                                     sizeof(g_vision_nodes[0])));
    return rc;
}

static int vision_exec_start(const bm_exec_t *instance) {
    (void)instance;
    return BM_OK;
}

static void vision_exec_safe_stop(const bm_exec_t *instance) {
    vision_axis_state_t *state = (vision_axis_state_t *)instance->state;

    if (state != NULL) {
        state->enabled = 0;
    }
}

static const bm_exec_ops_t g_vision_ops = {
    vision_exec_init, vision_exec_start, vision_exec_safe_stop
};

static const bm_exec_t g_vision_exec = {
    1u, "vision_frame", &g_vision_state, NULL, NULL,
    g_vision_slots, 2u, NULL, 0u, &g_vision_ops
};

static const bm_exec_t *const g_instances[] = { &g_vision_exec };

static const bm_ticker_slot_t g_poll_ticker[] = {
    { VISION_POLL_MS, EVENT_VISION_POLL, 1u, "tel_poll" }
};

void app_vision_enable_production(void) {
    g_vision_state.enabled = 1;
    g_vision_metrics.enable_events++;
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

    BM_LOGI(TAG, "stream_vision_frame example start");
    bm_hal_uart_init(NULL);

    rc = bm_module_boot();
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VISION_FRAME: FAIL boot\n");
        return 1;
    }

    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);
    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VISION_FRAME: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VISION_FRAME: FAIL start\n");
        return 1;
    }
    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        hybrid_print("EXAMPLE_STREAM_VISION_FRAME: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

#ifdef NATIVE_SIM
    run_sim(45000u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(VISION_QEMU_SIM_CYCLES);
#else
    run_sim(5000u);
#endif

    stats = bm_stream_stats(&g_vision_stream);
    if (g_vision_metrics.blocks_processed < VISION_PASS_MIN_BLOCKS ||
        g_vision_metrics.motion_frames < VISION_PASS_MOTION_MIN ||
        g_vision_metrics.enable_events == 0u ||
        stats == NULL || stats->corrupt != 0u) {
        hybrid_print("EXAMPLE_STREAM_VISION_FRAME: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    BM_LOGI(TAG, "ok: blocks=%u motion=%u cx=%.1f cy=%.1f",
            (unsigned)g_vision_metrics.blocks_processed,
            (unsigned)g_vision_metrics.motion_frames,
            (double)g_vision_metrics.last_cx,
            (double)g_vision_metrics.last_cy);
    hybrid_print_pass("STREAM_VISION_FRAME");
    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}

/**
 * @file daq_frontend.c
 * @brief DAQ 前端组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 *
 */
#include "bm/component/daq_frontend.h"

#include <math.h>

static void pre_trigger_push(bm_daq_frontend_axis_t *axis, float sample) {
    bm_daq_frontend_state_t *st = &axis->state;
    uint32_t cap;

    if (st->pre_trigger_buffer == NULL || st->pre_trigger_cap == 0u) {
        return;
    }

    cap = st->pre_trigger_cap;
    st->pre_trigger_buffer[st->pre_trigger_head] = sample;
    st->pre_trigger_head = (st->pre_trigger_head + 1u) % cap;
    if (st->pre_trigger_count < cap) {
        st->pre_trigger_count++;
    }
}

int bm_daq_frontend_init(bm_daq_frontend_axis_t *axis,
                         float *rms_buffer,
                         uint32_t rms_buflen,
                         float *pre_trigger_buffer,
                         uint32_t pre_trigger_buflen) {
    uint32_t cap;

    if (axis == NULL || rms_buffer == NULL || rms_buflen == 0u) {
        return BM_ERR_INVALID;
    }
    if (axis->config.pre_trigger_samples > 0u &&
        (pre_trigger_buffer == NULL || pre_trigger_buflen == 0u)) {
        return BM_ERR_INVALID;
    }

    axis->state.rms_buffer = rms_buffer;
    axis->state.rms_buflen = rms_buflen;
    axis->state.rms_cfg.window_samples = rms_buflen;
    if (bm_algo_rms_init(&axis->state.rms, &axis->state.rms_cfg,
                         rms_buffer, rms_buflen) != 0) {
        return BM_ERR_INVALID;
    }

    cap = pre_trigger_buflen;
    if (axis->config.pre_trigger_samples > 0u &&
        axis->config.pre_trigger_samples < cap) {
        cap = axis->config.pre_trigger_samples;
    }
    axis->state.pre_trigger_buffer = pre_trigger_buffer;
    axis->state.pre_trigger_cap = cap;

    bm_daq_frontend_reset(axis);
    return BM_OK;
}

void bm_daq_frontend_reset(bm_daq_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    bm_algo_rms_reset(&axis->state.rms);
    axis->state.pre_trigger_head = 0u;
    axis->state.pre_trigger_count = 0u;
    axis->state.peak = 0.0f;
    axis->state.crest_factor = 0.0f;
    axis->state.captured = 0u;
    axis->state.armed = 0;
    axis->state.triggered = 0;
}

void bm_daq_frontend_arm(bm_daq_frontend_axis_t *axis) {
    if (axis != NULL) {
        axis->state.armed = 1;
        axis->state.triggered = 0;
        axis->state.captured = 0u;
        axis->state.pre_trigger_head = 0u;
        axis->state.pre_trigger_count = 0u;
        axis->state.peak = 0.0f;
    }
}

int bm_daq_frontend_feed(bm_daq_frontend_axis_t *axis, float sample) {
    float rms;
    float ax;

    if (axis == NULL) {
        return BM_ERR_INVALID;
    }
    if (!axis->state.armed) {
        return BM_ERR_INVALID;
    }

    pre_trigger_push(axis, sample);

    rms = bm_algo_rms_step(&axis->state.rms, &axis->state.rms_cfg, sample);
    ax = fabsf(sample);
    if (ax > axis->state.peak) {
        axis->state.peak = ax;
    }

    if (!axis->state.triggered &&
        fabsf(sample) >= axis->config.trigger_level) {
        axis->state.triggered = 1;
    }

    if (axis->state.triggered) {
        axis->state.captured++;
        if (rms > 1e-9f) {
            axis->state.crest_factor = axis->state.peak / rms;
        }
        if (axis->state.captured >= axis->config.capture_samples) {
            axis->state.armed = 0;
            return BM_DAQ_CAPTURE_DONE;
        }
    }
    return BM_OK;
}

uint32_t bm_daq_frontend_copy_pre_trigger(const bm_daq_frontend_axis_t *axis,
                                          float *dst,
                                          uint32_t dst_len) {
    const bm_daq_frontend_state_t *st;
    uint32_t cap;
    uint32_t n;
    uint32_t i;
    uint32_t start;

    if (axis == NULL || dst == NULL || dst_len == 0u) {
        return 0u;
    }

    st = &axis->state;
    if (st->pre_trigger_buffer == NULL || st->pre_trigger_cap == 0u ||
        st->pre_trigger_count == 0u) {
        return 0u;
    }

    cap = st->pre_trigger_cap;
    n = st->pre_trigger_count;
    if (n > dst_len) {
        n = dst_len;
    }
    if (n < cap) {
        for (i = 0u; i < n; i++) {
            dst[i] = st->pre_trigger_buffer[i];
        }
    } else {
        start = st->pre_trigger_head;
        for (i = 0u; i < n; i++) {
            dst[i] = st->pre_trigger_buffer[(start + i) % cap];
        }
    }
    return n;
}

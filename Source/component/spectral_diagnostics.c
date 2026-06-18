/**
 * @file spectral_diagnostics.c
 * @brief 振动频谱诊断组件实现
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
#include "bm/component/spectral_diagnostics.h"
#include "bm/common/bm_types.h"

#include <string.h>

int bm_spectral_diagnostics_validate_config(
    const bm_spectral_diagnostics_config_t *config) {
    if (config == NULL || config->sample_hz <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->mode == BM_SPECTRAL_MODE_STFT) {
        if (config->stft_frame_size == 0u || config->stft_frame == NULL ||
            config->stft_window == NULL || config->stft_magnitude == NULL) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

void bm_spectral_diagnostics_reset(bm_spectral_diagnostics_axis_t *axis) {
    if (axis == NULL) {
        return;
    }

    bm_algo_goertzel_reset(&axis->state.goertzel);
    axis->state.frame_fill = 0u;
    axis->state.step_count = 0u;
    axis->state.goertzel_mag = 0.0f;
    axis->state.order = 0.0f;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_spectral_diagnostics_init(bm_spectral_diagnostics_axis_t *axis) {
    if (axis == NULL ||
        bm_spectral_diagnostics_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_goertzel_init(&axis->state.goertzel,
                              &axis->config.goertzel) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_spectral_diagnostics_reset(axis);
    return BM_OK;
}

void bm_spectral_diagnostics_step(bm_spectral_diagnostics_axis_t *axis,
                                  float shaft_rpm) {
    const bm_spectral_diagnostics_config_t *cfg;
    bm_spectral_diagnostics_state_t *st;
    float sample = 0.0f;
    int   goertzel_ready;

    if (axis == NULL) {
        return;
    }

    cfg = &axis->config;
    st = &axis->state;

    if (axis->resources.feed_sample != NULL &&
        axis->resources.feed_sample(axis->resources.feed_sample_user,
                                    &sample) != 0) {
        st->step_count++;
        st->telemetry.sequence = st->step_count;
        st->telemetry.status = BM_SPECTRAL_DIAG_TEL_STALE;
        st->telemetry.goertzel_mag = st->goertzel_mag;
        st->telemetry.order = st->order;
        st->telemetry.shaft_rpm = shaft_rpm;
        if (axis->resources.publish_telemetry != NULL) {
            axis->resources.publish_telemetry(
                axis->resources.publish_telemetry_user, &st->telemetry);
        }
        return;
    }

    goertzel_ready = bm_algo_goertzel_feed(&st->goertzel, &cfg->goertzel, sample);
    if (goertzel_ready) {
        st->goertzel_mag = bm_algo_goertzel_result(&st->goertzel, &cfg->goertzel);
        st->order = bm_algo_order_from_hz(cfg->goertzel.target_freq_hz,
                                          shaft_rpm, cfg->pole_pairs);
        bm_algo_goertzel_reset(&st->goertzel);
    }

    if (cfg->mode == BM_SPECTRAL_MODE_STFT && cfg->stft_frame != NULL) {
        cfg->stft_frame[st->frame_fill++] = sample;
        if (st->frame_fill >= cfg->stft_frame_size) {
            (void)bm_algo_stft_magnitude_frame(cfg->stft_frame, cfg->stft_window,
                                               cfg->stft_frame_size,
                                               cfg->stft_magnitude);
            st->frame_fill = 0u;
        }
    }

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    if (goertzel_ready) {
        st->telemetry.status = BM_SPECTRAL_DIAG_TEL_VALID;
    } else {
        st->telemetry.status = BM_SPECTRAL_DIAG_TEL_ACCUMULATING;
    }
    st->telemetry.goertzel_mag = st->goertzel_mag;
    st->telemetry.order = st->order;
    st->telemetry.shaft_rpm = shaft_rpm;

    if (axis->resources.publish_telemetry != NULL) {
        axis->resources.publish_telemetry(
            axis->resources.publish_telemetry_user, &st->telemetry);
    }
}

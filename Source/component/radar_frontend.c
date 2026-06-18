/**
 * @file radar_frontend.c
 * @brief 雷达快时间距离像处理实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 */
#include "bm/component/radar_frontend.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

int bm_radar_frontend_validate_config(
    const bm_radar_frontend_config_t *config) {
    if (config == NULL || config->sample_hz <= 0.0f ||
        config->fft_size == 0u ||
        !bm_algo_fft_is_supported_size(config->fft_size)) {
        return BM_ERR_INVALID;
    }
    if (config->clutter_alpha < 0.0f || config->clutter_alpha > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_radar_frontend_reset(bm_radar_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    if (axis->state.clutter_mean != NULL &&
        axis->state.profile_len > 0u) {
        memset(axis->state.clutter_mean, 0,
               (size_t)axis->state.profile_len * sizeof(float));
    }
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_radar_frontend_init(bm_radar_frontend_axis_t *axis,
                           float *profile,
                           uint32_t profile_len,
                           float *clutter_mean,
                           float *fft_work,
                           uint32_t fft_work_count) {
    if (axis == NULL || profile == NULL || clutter_mean == NULL ||
        fft_work == NULL ||
        profile_len < axis->config.fft_size ||
        bm_radar_frontend_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (fft_work_count < 2u * axis->config.fft_size) {
        return BM_ERR_INVALID;
    }
    if (bm_algo_rfft_f32_init(&axis->state.fft, axis->config.fft_size,
                              fft_work, fft_work_count) != 0) {
        return BM_ERR_INVALID;
    }

    axis->state.profile = profile;
    axis->state.profile_len = profile_len;
    axis->state.clutter_mean = clutter_mean;
    bm_radar_frontend_reset(axis);
    return BM_OK;
}

int bm_radar_frontend_feed_chirp(bm_radar_frontend_axis_t *axis,
                                 const float *chirp_samples,
                                 uint32_t sample_count) {
    const bm_radar_frontend_config_t *cfg;
    bm_radar_frontend_state_t *st;
    uint32_t fft_n;
    uint32_t bins;
    uint32_t i;
    uint32_t peak_bin = 0u;
    float peak_mag = 0.0f;
    float alpha;
    float range_scale;

    if (axis == NULL || chirp_samples == NULL || sample_count == 0u) {
        return BM_ERR_INVALID;
    }
    if (bm_radar_frontend_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    cfg = &axis->config;
    st = &axis->state;
    fft_n = cfg->fft_size;
    if (sample_count < fft_n) {
        return BM_ERR_INVALID;
    }

    if (bm_algo_rfft_f32_execute(&st->fft, chirp_samples, st->profile) != 0) {
        return BM_ERR_INVALID;
    }

    bins = fft_n / 2u;
    alpha = cfg->clutter_alpha;
    for (i = 0u; i < bins; ++i) {
        float mag = st->profile[i];

        st->clutter_mean[i] += alpha * (mag - st->clutter_mean[i]);
        st->profile[i] = mag - st->clutter_mean[i];
        if (st->profile[i] < 0.0f) {
            st->profile[i] = 0.0f;
        }
        if (st->profile[i] > peak_mag) {
            peak_mag = st->profile[i];
            peak_bin = i;
        }
    }

    range_scale = (cfg->range_scale_m > 0.0f)
                      ? cfg->range_scale_m
                      : (3.0e8f / (2.0f * cfg->sample_hz));

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.peak_bin = peak_bin;
    st->telemetry.peak_magnitude = peak_mag;
    st->telemetry.peak_range_m = (float)peak_bin * range_scale;
    return BM_OK;
}

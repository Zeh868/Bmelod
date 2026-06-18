/**
 * @file audio_array_frontend.c
 * @brief 麦克风阵列 DAS/MVDR 波束成形实现
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-17       0.2            zeh            MVDR 波束模式
 */
#include "bm/component/audio_array_frontend.h"
#include "bm/algorithm/bm_algo_audio.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

static float compute_energy(const float *samples, uint32_t n) {
    uint32_t i;
    float e = 0.0f;

    if (samples == NULL || n == 0u) {
        return 0.0f;
    }
    for (i = 0u; i < n; ++i) {
        e += samples[i] * samples[i];
    }
    return e / (float)n;
}

static void update_delays(bm_audio_array_frontend_axis_t *axis,
                          const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS]) {
    const bm_audio_array_frontend_config_t *cfg = &axis->config;
    bm_audio_array_frontend_state_t *st = &axis->state;
    uint32_t ch;
    uint32_t n = cfg->block_samples;

    if (cfg->use_fixed_delay) {
        for (ch = 0u; ch < cfg->num_channels; ++ch) {
            st->active_delays[ch] = cfg->fixed_delay_samples[ch];
        }
        return;
    }

    if (cfg->num_channels < 2u || st->gcc_work == NULL ||
        st->gcc_work_count == 0u) {
        memset(st->active_delays, 0, sizeof(st->active_delays));
        return;
    }

    st->active_delays[0] = 0;
    for (ch = 1u; ch < cfg->num_channels; ++ch) {
        int32_t lag = bm_algo_gcc_phat_delay(
            channels[0], channels[ch], n, cfg->max_gcc_lag,
            st->gcc_work, st->gcc_work_count);
        st->active_delays[ch] = (lag != BM_ALGO_GCC_PHAT_DELAY_INVALID)
                                   ? lag
                                   : 0;
    }
}

int bm_audio_array_frontend_validate_config(
    const bm_audio_array_frontend_config_t *config) {
    if (config == NULL || config->num_channels == 0u ||
        config->num_channels > BM_AUDIO_ARRAY_MAX_CHANNELS ||
        config->block_samples == 0u || config->sample_hz <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (!config->use_fixed_delay && config->max_gcc_lag < 0) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_audio_array_frontend_reset(bm_audio_array_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    memset(axis->state.active_delays, 0, sizeof(axis->state.active_delays));
    axis->state.last_energy = 0.0f;
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

int bm_audio_array_frontend_init(bm_audio_array_frontend_axis_t *axis,
                                 float *beam_buffer,
                                 uint32_t beam_buffer_len,
                                 float *gcc_work,
                                 uint32_t gcc_work_count) {
    if (axis == NULL || beam_buffer == NULL ||
        beam_buffer_len < axis->config.block_samples ||
        bm_audio_array_frontend_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (!axis->config.use_fixed_delay) {
        uint32_t need = bm_algo_gcc_phat_work_count(
            axis->config.block_samples, axis->config.max_gcc_lag);
        if (gcc_work == NULL || gcc_work_count < need) {
            return BM_ERR_INVALID;
        }
    }

    axis->state.beam_buffer = beam_buffer;
    axis->state.beam_buffer_len = beam_buffer_len;
    axis->state.gcc_work = gcc_work;
    axis->state.gcc_work_count = gcc_work_count;
    bm_audio_array_frontend_reset(axis);
    return BM_OK;
}

int bm_audio_array_frontend_step(bm_audio_array_frontend_axis_t *axis,
                                 const float *channels[BM_AUDIO_ARRAY_MAX_CHANNELS],
                                 float *mono_out,
                                 uint32_t out_cap) {
    const bm_audio_array_frontend_config_t *cfg;
    bm_audio_array_frontend_state_t *st;
    uint32_t n;
    uint32_t ch;
    float energy;

    if (axis == NULL || channels == NULL || mono_out == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_audio_array_frontend_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    cfg = &axis->config;
    st = &axis->state;
    n = cfg->block_samples;
    if (out_cap < n) {
        return BM_ERR_INVALID;
    }
    for (ch = 0u; ch < cfg->num_channels; ++ch) {
        if (channels[ch] == NULL) {
            return BM_ERR_INVALID;
        }
    }

    update_delays(axis, channels);

    if (cfg->beam_mode == BM_AUDIO_BEAM_MVDR) {
        bm_algo_mvdr_config_t mvdr_cfg = {
            .diagonal_load = (cfg->mvdr_diagonal_load > 0.0f)
                                 ? cfg->mvdr_diagonal_load
                                 : 1e-3f,
            .sample_hz = cfg->sample_hz
        };
        bm_algo_mvdr_beamform(channels, st->active_delays, cfg->num_channels,
                              n, &mvdr_cfg, mono_out);
    } else {
        bm_algo_delay_and_sum(channels, st->active_delays, cfg->num_channels,
                              n, mono_out);
    }

    energy = compute_energy(mono_out, n);
    st->last_energy = energy;
    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.energy = energy;
    for (ch = 0u; ch < BM_AUDIO_ARRAY_MAX_CHANNELS; ++ch) {
        st->telemetry.delay_samples[ch] = st->active_delays[ch];
    }
    return BM_OK;
}

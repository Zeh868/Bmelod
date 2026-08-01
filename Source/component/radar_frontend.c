/**
 * @file radar_frontend.c
 * @brief 雷达快时间距离像处理实现
 *
 * 对 chirp 块执行 RFFT 得到距离幅度谱，简易均值相减杂波抑制后
 * 输出峰值 bin 与峰值距离遥测。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.3
 * @date 2026-07-09
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 * 2026-07-09       0.3            zeh            杂波抑制/峰值搜索补 Nyquist bin（疑似-16.5）
 * 2026-08-01       0.3            Codex           补全 Doxygen 合规注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/radar_frontend.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/**
 * @brief 校验雷达前端配置参数
 *
 * @param config 配置结构指针（不可为 NULL）
 * @return BM_OK 参数合法；BM_ERR_INVALID 参数无效
 */
int bm_radar_frontend_validate_config(
    const bm_radar_frontend_config_t *config) {
    if (config == NULL || config->sample_hz <= 0.0f ||
        config->fft_size == 0u ||
        !bm_algo_fft_is_supported_size(config->fft_size)) {
        return BM_ERR_INVALID;
    }
    if (!bm_algo_is_finite_f(config->clutter_alpha) ||
        config->clutter_alpha < 0.0f || config->clutter_alpha > 1.0f) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 复位雷达前端状态（清零杂波均值缓冲与遥测）
 *
 * @param axis 轴实例指针（NULL 时直接返回）
 */
void bm_radar_frontend_reset(bm_radar_frontend_axis_t *axis) {
    if (axis == NULL) {
        return;
    }
    if (axis->state.clutter_mean != NULL &&
        axis->config.fft_size > 0u) {
        /* clutter_mean 契约长度为 fft_size/2（见 feed_chirp 中 bins 的写入范围），
         * 而非 profile_len（distance profile 缓冲长度，≥ fft_size），
         * 用 profile_len 计算会越界写 clutter_mean 缓冲之外的内存。 */
        memset(axis->state.clutter_mean, 0,
               ((size_t)axis->config.fft_size / 2u) * sizeof(float));
    }
    axis->state.step_count = 0u;
    memset(&axis->state.telemetry, 0, sizeof(axis->state.telemetry));
}

/**
 * @brief 初始化雷达前端（绑定缓冲并初始化 RFFT 实例）
 *
 * @param axis          轴实例指针（不可为 NULL；config 须预先填写）
 * @param profile       距离像输出缓冲，长度 ≥ fft_size
 * @param profile_len   profile 缓冲元素数
 * @param clutter_mean  杂波均值缓冲，长度 = fft_size/2
 * @param fft_work      RFFT 工作缓冲
 * @param fft_work_count fft_work 元素数，须 ≥ 2*fft_size
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或 RFFT 初始化失败
 */
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

/**
 * @brief 馈入一帧 chirp 采样并更新距离像与遥测
 *
 * 执行 RFFT → 幅度均值相减杂波抑制 → 负值截断 → 峰值搜索，
 * 结果写入 axis->state.profile 及 axis->state.telemetry。
 *
 * @param axis          已初始化的轴实例（不可为 NULL）
 * @param chirp_samples 输入 chirp 原始采样（不可为 NULL）
 * @param sample_count  采样点数，须 ≥ config.fft_size
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或 FFT 执行失败
 */
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
    /* NaN 会绕过 [0,1] 校验并永久污染杂波均值；运行时再做 isfinite+clamp 防护 */
    if (!bm_algo_is_finite_f(alpha)) {
        alpha = 0.0f;
    }
    alpha = bm_algo_clamp_f(alpha, 0.0f, 1.0f);
    /* 疑似-16.5：RFFT 实际写到索引 bins（Nyquist bin，见 bm_algo_rfft_f32_execute
     * 的 i<=fft->size/2 循环），但此前峰值搜索只扫 i<bins，遗漏该 bin——若目标
     * 峰值恰好落在 Nyquist 频率会漏检。clutter_mean 契约长度为 bins（无
     * Nyquist 槽位），故 i==bins 时不做杂波抑制，直接以原始（非负）幅值
     * 参与峰值搜索。 */
    for (i = 0u; i <= bins; ++i) {
        float mag = st->profile[i];

        if (i < bins) {
            st->clutter_mean[i] += alpha * (mag - st->clutter_mean[i]);
            st->profile[i] = mag - st->clutter_mean[i];
            if (st->profile[i] < 0.0f) {
                st->profile[i] = 0.0f;
            }
        }
        if (st->profile[i] > peak_mag) {
            peak_mag = st->profile[i];
            peak_bin = i;
        }
    }

    range_scale = (cfg->range_scale_m > 0.0f)
                      ? cfg->range_scale_m
                      : (BM_SPEED_OF_LIGHT_M_S / (2.0f * cfg->sample_hz));

    st->step_count++;
    st->telemetry.sequence = st->step_count;
    st->telemetry.peak_bin = peak_bin;
    st->telemetry.peak_magnitude = peak_mag;
    st->telemetry.peak_range_m = (float)peak_bin * range_scale;
    return BM_OK;
}

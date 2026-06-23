/**
 * @file radar_frontend.h
 * @brief 雷达前端：快时间 FFT 距离像与杂波抑制
 *
 * 对 chirp 块做 RFFT 得到距离幅度谱，简易均值相减杂波抑制后输出峰值。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-17
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            补 SPDX 与函数级 Doxygen
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_RADAR_FRONTEND_H
#define BM_RADAR_FRONTEND_H

#include "bm/algorithm/bm_algo_fft.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t fft_size;
    float    sample_hz;
    float    clutter_alpha;
    float    range_scale_m;
} bm_radar_frontend_config_t;

typedef struct {
    uint32_t sequence;
    uint32_t peak_bin;
    float    peak_magnitude;
    float    peak_range_m;
} bm_radar_frontend_telemetry_t;

typedef struct {
    bm_algo_rfft_f32_t fft;
    float             *profile;
    uint32_t           profile_len;
    float             *clutter_mean;
    uint32_t           step_count;
    bm_radar_frontend_telemetry_t telemetry;
} bm_radar_frontend_state_t;

typedef struct {
    bm_radar_frontend_config_t config;
    bm_radar_frontend_state_t  state;
} bm_radar_frontend_axis_t;

/**
 * @brief 校验雷达前端配置参数
 * @param config 配置结构指针（不可为 NULL）
 * @return BM_OK 合法；BM_ERR_INVALID 无效
 */
int  bm_radar_frontend_validate_config(
    const bm_radar_frontend_config_t *config);

/**
 * @brief 初始化雷达前端（绑定缓冲并初始化 RFFT 实例）
 * @param axis           轴实例（不可为 NULL；config 须预先填写）
 * @param profile        距离像输出缓冲，长度 ≥ fft_size
 * @param profile_len    profile 缓冲元素数
 * @param clutter_mean   杂波均值缓冲，长度 = fft_size/2
 * @param fft_work       RFFT 工作缓冲
 * @param fft_work_count fft_work 元素数，须 ≥ 2*fft_size
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int  bm_radar_frontend_init(bm_radar_frontend_axis_t *axis,
                            float *profile,
                            uint32_t profile_len,
                            float *clutter_mean,
                            float *fft_work,
                            uint32_t fft_work_count);

/**
 * @brief 复位雷达前端状态（清零杂波均值缓冲与遥测）
 * @param axis 轴实例指针（NULL 时直接返回）
 */
void bm_radar_frontend_reset(bm_radar_frontend_axis_t *axis);

/**
 * @brief 馈入一帧 chirp 采样并更新距离像与遥测
 * @param axis          已初始化的轴实例（不可为 NULL）
 * @param chirp_samples 输入 chirp 原始采样（不可为 NULL）
 * @param sample_count  采样点数，须 ≥ config.fft_size
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效或 FFT 执行失败
 */
int  bm_radar_frontend_feed_chirp(bm_radar_frontend_axis_t *axis,
                                  const float *chirp_samples,
                                  uint32_t sample_count);

#ifdef __cplusplus
}
#endif

#endif /* BM_RADAR_FRONTEND_H */

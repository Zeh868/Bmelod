/**
 * @file spectral_diagnostics.h
 * @brief 振动频谱诊断（Goertzel / STFT / 阶次跟踪）
 *
 * 支持两种工作模式：
 *   - BM_SPECTRAL_MODE_GOERTZEL：Goertzel 单频幅值检测，适合已知特征频率；
 *   - BM_SPECTRAL_MODE_STFT：逐帧 STFT 幅度谱，stft_frame_size 须为
 *     bm_algo_fft_is_supported_size 认可的点数（64/128/256/512/1024）。
 *
 * 两种模式均支持阶次换算（bm_algo_order_from_hz），通过 shaft_rpm 参数传入
 * 机械转速。
 *
 * exec_ops 不适用于本组件（流式诊断，无周期 step 语义）。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 0.2
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-23       0.2            zeh            STFT frame_size 校验说明；Doxygen；SPDX
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_SPECTRAL_DIAGNOSTICS_H
#define BM_SPECTRAL_DIAGNOSTICS_H

#include "bm/algorithm/bm_algo_spectral.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BM_SPECTRAL_DIAG_TEL_VALID        (1u << 0u)
#define BM_SPECTRAL_DIAG_TEL_STALE        (1u << 1u)
#define BM_SPECTRAL_DIAG_TEL_ACCUMULATING (1u << 2u)

#define BM_SPECTRAL_DIAG_MAX_BINS  64u

typedef struct {
    uint32_t sequence;
    uint32_t status;
    float    goertzel_mag;
    float    order;
    float    shaft_rpm;
} bm_spectral_diagnostics_telemetry_t;

typedef int (*bm_spectral_diagnostics_feed_fn)(void *user, float *sample);

typedef void (*bm_spectral_diagnostics_publish_fn)(
    void *user,
    const bm_spectral_diagnostics_telemetry_t *telemetry);

typedef struct {
    bm_spectral_diagnostics_feed_fn    feed_sample;
    void                              *feed_sample_user;
    bm_spectral_diagnostics_publish_fn publish_telemetry;
    void                              *publish_telemetry_user;
} bm_spectral_diagnostics_resources_t;

typedef enum {
    BM_SPECTRAL_MODE_GOERTZEL = 0,
    BM_SPECTRAL_MODE_STFT
} bm_spectral_diagnostics_mode_t;

typedef struct {
    bm_spectral_diagnostics_mode_t mode;
    bm_algo_goertzel_config_t      goertzel;
    float                          sample_hz;
    uint32_t                       stft_frame_size;
    float                          pole_pairs;
    float                         *stft_frame;
    float                         *stft_window;
    float                         *stft_magnitude;
} bm_spectral_diagnostics_config_t;

typedef struct {
    bm_algo_goertzel_state_t goertzel;
    uint32_t                 frame_fill;
    uint32_t                 step_count;
    float                    goertzel_mag;
    float                    order;
    bm_spectral_diagnostics_telemetry_t telemetry;
} bm_spectral_diagnostics_state_t;

typedef struct {
    bm_spectral_diagnostics_config_t    config;
    bm_spectral_diagnostics_resources_t resources;
    bm_spectral_diagnostics_state_t     state;
} bm_spectral_diagnostics_axis_t;

/**
 * @brief 校验频谱诊断配置参数合法性
 *
 * STFT 模式下额外检查 stft_frame_size 是否为 FFT 支持的合法点数。
 *
 * @param config 配置指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int  bm_spectral_diagnostics_validate_config(
    const bm_spectral_diagnostics_config_t *config);

/**
 * @brief 初始化轴（校验配置 + 初始化 Goertzel + 复位状态）
 *
 * @param axis 轴实例指针，NULL 时返回 BM_ERR_INVALID
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int  bm_spectral_diagnostics_init(bm_spectral_diagnostics_axis_t *axis);

/**
 * @brief 复位内部状态（不重新校验配置，不重新初始化 Goertzel 系数）
 *
 * @param axis 轴实例指针，NULL 时静默返回
 */
void bm_spectral_diagnostics_reset(bm_spectral_diagnostics_axis_t *axis);

/**
 * @brief 单步处理：读采样、Goertzel 累积、STFT 帧填充与计算、发布遥测
 *
 * @param axis      轴实例指针，NULL 时静默返回
 * @param shaft_rpm 当前机械转速（rpm），用于阶次换算
 */
void bm_spectral_diagnostics_step(bm_spectral_diagnostics_axis_t *axis,
                                  float shaft_rpm);

#ifdef __cplusplus
}
#endif

#endif /* BM_SPECTRAL_DIAGNOSTICS_H */

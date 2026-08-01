/**
 * @file bm_algo_filter.h
 * @brief 滤波算法：一阶 LPF/HPF、滑动平均、中值、FIR 与 Biquad/陷波
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 * 2026-06-17       1.1            zeh            动态 notch 系数更新
 * 2026-07-27       1.2            zeh            新增 bm_algo_lpf1_alpha_saturate
 * 2026-07-28       1.3            zeh            状态返回文档对齐 BM_OK/BM_ERR_*
 * 2026-08-01       1.3            zeh           补全算法 API Doxygen 注释
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_ALGO_FILTER_H
#define BM_ALGO_FILTER_H

#include "bm/algorithm/bm_algo_errors.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 一阶低通 ---------- */
typedef struct {
    float alpha;  /**< 0<alpha<=1，越大跟踪越快 */
} bm_algo_lpf1_config_t;

typedef struct {
    float output;
} bm_algo_lpf1_state_t;

/**
 * @brief 根据截止频率初始化一阶低通滤波器
 * @param config 算法配置
 * @param cutoff_hz 截止频率，单位 Hz
 * @param sample_hz 采样频率，单位 Hz
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_algo_lpf1_init_from_cutoff(bm_algo_lpf1_config_t *config,
                                  float cutoff_hz,
                                  float sample_hz);
/**
 * @brief 复位一阶低通滤波器状态
 * @param state 算法运行状态
 * @param value 输入值
 */
void bm_algo_lpf1_reset(bm_algo_lpf1_state_t *state, float value);
/**
 * @brief 更新一阶低通滤波器并计算本拍输出
 * @param state 算法运行状态
 * @param config 算法配置
 * @param input 当前输入值
 * @return 一阶低通滤波器计算得到的浮点输出
 */
float bm_algo_lpf1_step(bm_algo_lpf1_state_t *state,
                        const bm_algo_lpf1_config_t *config,
                        float input);

/** @brief 一阶 LPF alpha 下界（<=0 时取该极小正值，保证滤波器仍有跟踪能力） */
#define BM_ALGO_LPF1_ALPHA_MIN 1e-6f

/**
 * @brief 将一阶 LPF 的 alpha 系数饱和到 (0, 1]
 *
 * 收敛各算法中手动 clamp 到 [0,1] 的重复代码；非有限值或 alpha <= 0 时
 * 取极小值 BM_ALGO_LPF1_ALPHA_MIN，alpha > 1 时截断至 1.0。
 *
 * @param alpha 输入 alpha 系数
 * @return 饱和后的 alpha 系数，范围 (0, 1]
 */
float bm_algo_lpf1_alpha_saturate(float alpha);

/* ---------- 一阶高通（由低通推导） ---------- */
typedef struct {
    float alpha;
} bm_algo_hpf1_config_t;

typedef struct {
    float prev_input;
    float prev_output;
} bm_algo_hpf1_state_t;

/**
 * @brief 根据截止频率初始化一阶高通滤波器
 * @param config 算法配置
 * @param cutoff_hz 截止频率，单位 Hz
 * @param sample_hz 采样频率，单位 Hz
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_algo_hpf1_init_from_cutoff(bm_algo_hpf1_config_t *config,
                                  float cutoff_hz,
                                  float sample_hz);
/**
 * @brief 复位一阶高通滤波器状态
 * @param state 算法运行状态
 */
void bm_algo_hpf1_reset(bm_algo_hpf1_state_t *state);
/**
 * @brief 更新一阶高通滤波器并计算本拍输出
 * @param state 算法运行状态
 * @param config 算法配置
 * @param input 当前输入值
 * @return 一阶高通滤波器计算得到的浮点输出
 */
float bm_algo_hpf1_step(bm_algo_hpf1_state_t *state,
                        const bm_algo_hpf1_config_t *config,
                        float input);

/* ---------- 滑动平均 ---------- */
typedef struct {
    float *buffer;
    uint32_t length;
} bm_algo_moving_avg_config_t;

typedef struct {
    float sum;
    uint32_t index;
    uint32_t count;
    uint32_t length;
} bm_algo_moving_avg_state_t;

/**
 * @brief 初始化滑动平均滤波器
 * @param state 算法运行状态
 * @param config 算法配置
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_algo_moving_avg_init(bm_algo_moving_avg_state_t *state,
                            const bm_algo_moving_avg_config_t *config);
/**
 * @brief 复位滑动平均滤波器状态
 * @param state 算法运行状态
 * @param config 算法配置
 */
void bm_algo_moving_avg_reset(bm_algo_moving_avg_state_t *state,
                              const bm_algo_moving_avg_config_t *config);
/**
 * @brief 更新滑动平均滤波器并计算本拍输出
 * @param state 算法运行状态
 * @param config 算法配置
 * @param input 当前输入值
 * @return 滑动平均滤波器计算得到的浮点输出
 */
float bm_algo_moving_avg_step(bm_algo_moving_avg_state_t *state,
                              const bm_algo_moving_avg_config_t *config,
                              float input);

/* ---------- 中值滤波（奇数窗口，最大 9 点） ---------- */
#define BM_ALGO_MEDIAN_MAX_TAPS 9u

typedef struct {
    uint32_t length;  /**< 3/5/7/9 */
} bm_algo_median_config_t;

typedef struct {
    float buffer[BM_ALGO_MEDIAN_MAX_TAPS];
    uint32_t index;
    uint32_t count;
} bm_algo_median_state_t;

/**
 * @brief 复位中值滤波器状态
 * @param state 算法运行状态
 */
void bm_algo_median_reset(bm_algo_median_state_t *state);
/**
 * @brief 更新中值滤波器并计算本拍输出
 * @param state 算法运行状态
 * @param config 算法配置
 * @param input 当前输入值
 * @return 中值滤波器计算得到的浮点输出
 */
float bm_algo_median_step(bm_algo_median_state_t *state,
                          const bm_algo_median_config_t *config,
                          float input);

/* ---------- 直接型 FIR ---------- */
typedef struct {
    const float *coeffs;
    uint32_t tap_count;
    float *delay_line;
} bm_algo_fir_config_t;

typedef struct {
    uint32_t index;
    uint32_t tap_count;
} bm_algo_fir_state_t;

/**
 * @brief 初始化 FIR 滤波器
 * @param state 算法运行状态
 * @param config 算法配置
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_algo_fir_init(bm_algo_fir_state_t *state,
                     const bm_algo_fir_config_t *config);
/**
 * @brief 复位 FIR 滤波器状态
 * @param state 算法运行状态
 * @param config 算法配置
 */
void bm_algo_fir_reset(bm_algo_fir_state_t *state,
                       const bm_algo_fir_config_t *config);
/**
 * @brief 更新 FIR 滤波器并计算本拍输出
 * @param state 算法运行状态
 * @param config 算法配置
 * @param input 当前输入值
 * @return FIR 滤波器计算得到的浮点输出
 */
float bm_algo_fir_step(bm_algo_fir_state_t *state,
                       const bm_algo_fir_config_t *config,
                       float input);

/* ---------- Biquad（直接 II 型） ---------- */
typedef enum {
    BM_ALGO_BIQUAD_LPF = 0,
    BM_ALGO_BIQUAD_HPF,
    BM_ALGO_BIQUAD_BPF,
    BM_ALGO_BIQUAD_NOTCH,
    BM_ALGO_BIQUAD_PEAKING
} bm_algo_biquad_type_t;

typedef struct {
    bm_algo_biquad_type_t type;
    float sample_hz;
    float freq_hz;
    float q;
    float gain_db;
} bm_algo_biquad_design_t;

typedef struct {
    float b0, b1, b2, a1, a2;
} bm_algo_biquad_config_t;

typedef struct {
    float z1;
    float z2;
} bm_algo_biquad_state_t;

/**
 * @brief 设计双二阶滤波器系数
 * @param config 算法配置
 * @param design 滤波器类型、采样率、中心频率、Q 值和增益等设计参数
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法
 */
int bm_algo_biquad_design(bm_algo_biquad_config_t *config,
                          const bm_algo_biquad_design_t *design);
/**
 * @brief 复位双二阶滤波器状态
 * @param state 算法运行状态
 */
void bm_algo_biquad_reset(bm_algo_biquad_state_t *state);
/**
 * @brief 更新双二阶滤波器并计算本拍输出
 * @param state 算法运行状态
 * @param config 算法配置
 * @param input 当前输入值
 * @return 双二阶滤波器计算得到的浮点输出
 */
float bm_algo_biquad_step(bm_algo_biquad_state_t *state,
                          const bm_algo_biquad_config_t *config,
                          float input);

/**
 * @brief 按新中心频率重算陷波 biquad 系数（保留状态）
 *
 * @param config 输出系数（不可为 NULL）
 * @param state 滤波器状态（可为 NULL）
 * @param sample_hz 采样率（Hz）
 * @param center_hz 陷波中心（Hz）
 * @param q 品质因数（>0）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效
 */
int bm_algo_biquad_notch_update(bm_algo_biquad_config_t *config,
                                bm_algo_biquad_state_t *state,
                                float sample_hz,
                                float center_hz,
                                float q);

#ifdef __cplusplus
}
#endif

#endif /* BM_ALGO_FILTER_H */
